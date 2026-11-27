const BASE_URL = "/api/v1";

async function request(path, method = "GET", body = null) {
    const options = {
        method,
        cache: "no-store"
    };

    if (body !== null) {
        options.headers = {
            "Content-Type": "application/json"
        };

        options.body = JSON.stringify(body);
    }

    const response = await fetch(`${BASE_URL}${path}`, options);
    const result = await response.json();

    if (!response.ok || result.status === "ERROR") {
        throw new Error(
            result.error?.message ?? `HTTP ${response.status}`
        );
    }

    return result;
}

async function get(path) {
    const result = await request(path);
    return result.data;
}

export const api = {
    getServices() {
        return get("/services");
    },

    getSession() {
        return get("/session");
    },

    getPlaybackStatus() {
        return get("/playback/status");
    },

    selectService(serviceId, generationId) {
        return request("/session/channel", "POST", {
            serviceId,
            generationId
        });
    },

    pause(generationId) {
        return request("/playback/pause", "POST", {
            generationId
        });
    },

    resume(generationId) {
        return request("/playback/resume", "POST", {
            generationId
        });
    },

    seek(generationId, targetMs) {
        return request("/playback/seek", "POST", {
            generationId,
            targetMs: Math.round(targetMs)
        });
    },

    goLive(generationId) {
        return request("/playback/live", "POST", {
            generationId
        });
    },

    acknowledgeSeek(generationId, seekRevision, positionMs) {
        return request("/playback/ack", "POST", {
            generationId,
            seekRevision,
            positionMs: Math.round(positionMs)
        });
    },

    reportPosition(generationId, seekRevision, positionMs) {
        return request("/playback/position", "POST", {
            generationId,
            seekRevision,
            positionMs: Math.round(positionMs)
        });
    },

    getEpg(serviceId, date) {
        return get(
            `/services/${serviceId}/epg?date=${encodeURIComponent(date)}`
        );
    },

    getHealth() {
        return get("/health");
    },

    connectEvents() {
        const protocol =
            location.protocol === "https:" ? "wss:" : "ws:";

        return new WebSocket(
            `${protocol}//${location.host}${BASE_URL}/events`
        );
    }
};
