import {
    openGuide,
    closeGuide,
    isGuideOpen
} from "./guide.js";
import { showInfo, toggleInfo, hideInfo } from "./info.js";
import Hls from "hls.js";
import { api } from "./api.js";

const $ = id => document.getElementById(id);
const video = $("video");
const timeline = $("timeline");
const seekInput = $("seek-position");

const clamp = (value, min, max) =>
    Math.min(Math.max(value, min), max);

const seconds = ms =>
    Number.isFinite(ms) ? `${(ms / 1000).toFixed(2)} s` : "--";

let services = [];
let session = null;

let hls = null;
let details = null;
let loadedGeneration = null;

let started = false;
let switching = false;
let refreshing = false;
let commanding = false;
let dragging = false;

let appliedRevision = null;
let pendingSeek = null;
let feedbackInProgress = false;
let eventSocket = null;
let reconnectTimer = null;
let shuttingDown = false;

function status(message) {
    $("status").textContent = message;
}

function updateControls() {
    const channelDisabled =
        switching || commanding || !session || services.length === 0;

    for (const id of [
        "previous-channel",
        "next-channel",
        "service-list",
        "channel-number",
        "select-channel"
    ]) {
        $(id).disabled = channelDisabled;
    }

    const playback = session?.playback;

    const ready =
        started &&
        !!hls &&
        !!details &&
        playback?.bufferAvailable &&
        video.readyState >= 1 &&
        !switching &&
        !commanding;

    $("start-button").disabled =
        !session?.hlsUrl || switching || (started && !video.paused);

    $("pause-button").disabled =
        !ready || playback?.state === "PAUSED";

    $("resume-button").disabled =
        !ready || playback?.state !== "PAUSED";

    for (const id of [
        "oldest-button",
        "live-button",
        "seek-button",
        "seek-position",
        "timeline"
    ]) {
        $(id).disabled = !ready;
    }
}

async function loadServices() {
    services = await api.getServices();

    services.sort(
        (a, b) => a.channelNumber - b.channelNumber
    );

    const list = $("service-list");
    list.replaceChildren();

    for (const service of services) {
        const option = document.createElement("option");

        option.value = String(service.serviceId);
        option.textContent =
            `${service.channelNumber} - ${service.name}`;

        list.appendChild(option);
    }
}

function mediaInterval() {
    const fragments = details?.fragments;

    if (!fragments?.length) return null;

    const first = fragments[0];
    const last = fragments[fragments.length - 1];

    return {
        start: first.start,
        end: last.start + last.duration
    };
}

function mediaOffset() {
    const first = details?.fragments?.[0];

    if (!first) return null;

    return Number(first.sn) * 2 - first.start;
}

function mediaPosition(positionMs) {
    const offset = mediaOffset();

    if (offset === null) return null;

    return positionMs / 1000 - offset;
}

function livePosition(interval) {
    const position = hls?.liveSyncPosition;

    return clamp(
        Number.isFinite(position)
            ? position
            : interval.end - 2,
        interval.start,
        Math.max(interval.start, interval.end - 0.2)
    );
}

function playVideo() {
    const instance = hls;

    video.play().catch(error => {
        if (hls !== instance) return;

        status(`Playback: ${error.message}`);
        updateControls();
    });
}

function destroyPlayer() {
    const old = hls;

    hls = null;
    details = null;
    loadedGeneration = null;
    appliedRevision = null;
    pendingSeek = null;

    if (old) old.destroy();

    video.pause();
    video.removeAttribute("src");
    video.load();

    $("resolution").textContent = "Unknown";
}

function loadPlayer() {
    if (
        !started ||
        !session?.hlsUrl ||
        !session.playback.bufferAvailable
    ) {
        return;
    }

    if (
        hls &&
        loadedGeneration === session.generationId
    ) {
        return;
    }

    destroyPlayer();

    if (!Hls.isSupported()) {
        status("HLS.js / MSE not supported");
        return;
    }

    const url = session.hlsUrl;

    const instance = new Hls({
        lowLatencyMode: false,
        liveSyncDurationCount: 1,
        liveMaxLatencyDurationCount: Infinity,
        maxLiveSyncPlaybackRate: 1,
        maxBufferLength: 10,
        backBufferLength: 10
    });

    hls = instance;
    loadedGeneration = session.generationId;

    instance.on(Hls.Events.MEDIA_ATTACHED, () => {
        if (hls === instance) {
            instance.loadSource(url);
        }
    });

    instance.on(Hls.Events.MANIFEST_PARSED, () => {
        if (hls !== instance) return;

        if (session?.playback.state !== "PAUSED") {
            playVideo();
        }
    });

    instance.on(Hls.Events.LEVEL_UPDATED, (_, data) => {
        if (hls !== instance) return;

        details = data.details;

        synchronizePlayback();
        updateMetrics();
        updateControls();
    });

    instance.on(Hls.Events.ERROR, (_, data) => {
        if (hls !== instance) return;

        console.warn("HLS", data);

        if (data.fatal) {
            destroyPlayer();
            started = false;

            status(`HLS error: ${data.details}`);
            updateControls();
        }
    });

    instance.attachMedia(video);

    status("Loading HLS stream...");
}

function synchronizePlayback() {
    if (
        !started ||
        !hls ||
        !details ||
        !session ||
        switching ||
        video.readyState < 1
    ) {
        return;
    }

    const playback = session.playback;
    const interval = mediaInterval();

    if (!interval || !playback.bufferAvailable) return;

    if (
        playback.seekRequired &&
        appliedRevision !== playback.seekRevision
    ) {
        let target = mediaPosition(playback.positionMs);

        if (target === null) return;

        if (playback.atLiveEdge) {
            target = livePosition(interval);
        }

        target = clamp(
            target,
            interval.start,
            Math.max(interval.start, interval.end - 0.2)
        );

        appliedRevision = playback.seekRevision;

        pendingSeek = {
            generationId: session.generationId,
            revision: playback.seekRevision,
            target,
            startedAt: performance.now()
        };

        video.currentTime = target;

        status(`Seeking: ${target.toFixed(2)} s`);
    }

    if (!playback.seekRequired) {
        pendingSeek = null;
    }

    if (playback.state === "PAUSED") {
        if (!video.paused) video.pause();
    } else if (
        playback.state === "LIVE" ||
        playback.state === "TIMESHIFT"
    ) {
        if (video.paused) playVideo();
    }
}

async function sendPlaybackFeedback() {
    if (
        feedbackInProgress ||
        !started ||
        !session ||
        !hls ||
        switching ||
        commanding ||
        video.seeking ||
        video.readyState < 2 ||
        loadedGeneration !== session.generationId
    ) {
        return;
    }

    const playback = session.playback;
    const offset = mediaOffset();

    if (!playback.bufferAvailable || offset === null) {
        return;
    }

    const generationId = session.generationId;
    const revision = playback.seekRevision;

    const positionMs = clamp(
        Math.round((video.currentTime + offset) * 1000),
        playback.earliestMs,
        playback.latestMs
    );

    feedbackInProgress = true;

    try {
        if (playback.seekRequired) {
            if (
                !pendingSeek ||
                pendingSeek.generationId !== generationId ||
                pendingSeek.revision !== revision
            ) {
                return;
            }

            const elapsed =
                performance.now() - pendingSeek.startedAt;

            const difference =
                Math.abs(video.currentTime - pendingSeek.target);

            if (difference > 2) {
                if (elapsed > 4000) {
                    appliedRevision = null;
                    pendingSeek = null;
                    synchronizePlayback();
                }

                return;
            }

            await api.acknowledgeSeek(
                generationId,
                revision,
                positionMs
            );

        } else if (
            !video.paused &&
            (
                playback.state === "LIVE" ||
                playback.state === "TIMESHIFT"
            )
        ) {
            await api.reportPosition(
                generationId,
                revision,
                positionMs
            );
        }

    } catch (error) {
        console.warn("Playback feedback:", error.message);
    } finally {
        feedbackInProgress = false;
    }
}

function updateMetrics() {
    const playback = session?.playback;

    if (!playback?.bufferAvailable) {
        for (const id of [
            "buffer-start",
            "buffer-end",
            "buffer-duration",
            "playback-position",
            "live-position",
            "timeshift-delay",
            "live-edge-distance",
            "playback-time",
            "timeline-start",
            "timeline-end"
        ]) {
            $(id).textContent = "--";
        }

        return;
    }

    const offset = mediaOffset();

    const position =
        started && offset !== null && video.readyState >= 1
            ? Math.round((video.currentTime + offset) * 1000)
            : playback.positionMs;

    const delay = Math.max(0, playback.latestMs - position);

    const values = {
        "buffer-start": playback.earliestMs,
        "buffer-end": playback.latestMs,
        "buffer-duration": playback.bufferDurationMs,
        "playback-position": position,
        "live-position": playback.latestMs,
        "timeshift-delay": delay,
        "live-edge-distance": delay,
        "playback-time": position
    };

    for (const [id, value] of Object.entries(values)) {
        $(id).textContent = seconds(value);
    }

    if (!dragging) {
        timeline.min = String(playback.earliestMs / 1000);
        timeline.max = String(playback.latestMs / 1000);

        timeline.value = String(
            clamp(
                position,
                playback.earliestMs,
                playback.latestMs
            ) / 1000
        );

        $("timeline-start").textContent =
            seconds(playback.earliestMs);

        $("timeline-end").textContent =
            `Live ${seconds(playback.latestMs)}`;
    }

    const mode = $("playback-mode");

    mode.textContent = playback.state;
    mode.className =
        `metric-value mode-${playback.state.toLowerCase()}`;
}

function applySession(snapshot) {
    const generationChanged =
        session?.generationId !== snapshot.generationId;

    if (generationChanged) {
        destroyPlayer();
    }

    session = snapshot;

	if (generationChanged) {
	    hideInfo();

	    if (started && snapshot.activeService) {
	        showInfo(snapshot.activeService);
	    }
	}

    $("generation").textContent = snapshot.generationId;
    $("source").textContent = snapshot.hlsUrl ?? "--";

    $("channel-name").textContent = snapshot.activeService
        ? `${snapshot.activeService.channelNumber} - ${snapshot.activeService.name}`
        : "No active channel";

    if (snapshot.serviceId !== null) {
        $("service-list").value = String(snapshot.serviceId);
    }

    $("playback-mode").textContent = snapshot.playback.state;

    if (snapshot.state === "ERROR") {
        destroyPlayer();
        status(snapshot.errorMessage || "Session error");
    } else if (!snapshot.playback.bufferAvailable) {
        status("Waiting for stream...");
    } else if (!started) {
        status("Ready - press Start playback");
    } else {
        loadPlayer();
        synchronizePlayback();
    }

    updateMetrics();
    updateControls();
}

async function refreshSession() {
    if (refreshing || switching || commanding) return;

    refreshing = true;

    try {
        const snapshot = await api.getSession();

        if (!switching && !commanding) {
            applySession(snapshot);
        }
    } catch (error) {
        status(error.message);
    } finally {
        refreshing = false;
    }
}

async function selectService(serviceId) {
    if (switching || commanding || !session) return;

    if (serviceId === session.serviceId) return;

    switching = true;

    destroyPlayer();
    updateControls();

    status("Changing channel...");

    try {
        await api.selectService(
            serviceId,
            session.generationId
        );

        for (let attempt = 0; attempt < 30; attempt++) {
            await new Promise(
                resolve => setTimeout(resolve, 250)
            );

            const snapshot = await api.getSession();

            if (snapshot.state === "ERROR") {
                applySession(snapshot);

                throw new Error(
                    snapshot.errorMessage || "Channel change failed"
                );
            }

            if (
                snapshot.serviceId === serviceId &&
                snapshot.state === "RUNNING"
            ) {
                applySession(snapshot);
                return;
            }
        }

        throw new Error("Channel change timeout");

    } catch (error) {
        status(error.message);

        if (session) {
            $("service-list").value = String(session.serviceId);
        }
    } finally {
        switching = false;

        updateControls();
        refreshSession();
    }
}

function changeChannel(direction) {
    if (!session || services.length === 0) return;

    const index = services.findIndex(
        service => service.serviceId === session.serviceId
    );

    if (index < 0) return;

    const nextIndex =
        (index + direction + services.length) % services.length;

    selectService(services[nextIndex].serviceId);
}

function selectChannelNumber() {
    const number = Number($("channel-number").value);

    const service = services.find(
        item => item.channelNumber === number
    );

    if (!service) {
        status("Unknown channel number");
        return;
    }

    selectService(service.serviceId);
}

async function playbackCommand(command) {
    if (
        !session ||
        switching ||
        commanding
    ) {
        return;
    }

    commanding = true;
    updateControls();

    try {
        await command(session.generationId);
    } catch (error) {
        status(error.message);
    } finally {
        commanding = false;
        updateControls();
        refreshSession();
    }
}

function startPlayback() {
    if (!session?.hlsUrl) return;

    started = true;

    loadPlayer();

    playbackCommand(
        generationId => api.goLive(generationId)
    );
}

function seekToPosition(positionSeconds) {
    if (!Number.isFinite(positionSeconds)) return;

    playbackCommand(
        generationId =>
            api.seek(generationId, positionSeconds * 1000)
    );
}

$("previous-channel").onclick = () => changeChannel(-1);
$("next-channel").onclick = () => changeChannel(1);

$("service-list").onchange = event => {
    selectService(Number(event.target.value));
};

$("select-channel").onclick = selectChannelNumber;

$("channel-number").addEventListener("keydown", event => {
    if (event.key === "Enter") {
        selectChannelNumber();
    }
});

$("start-button").onclick = startPlayback;

$("pause-button").onclick = () => {
    playbackCommand(
        generationId => api.pause(generationId)
    );
};

$("resume-button").onclick = () => {
    playbackCommand(
        generationId => api.resume(generationId)
    );
};

$("live-button").onclick = () => {
    playbackCommand(
        generationId => api.goLive(generationId)
    );
};

$("oldest-button").onclick = () => {
    if (!session?.playback.bufferAvailable) return;

    seekToPosition(session.playback.earliestMs / 1000);
};

$("seek-button").onclick = () => {
    if (seekInput.value.trim() !== "") {
        seekToPosition(Number(seekInput.value));
    }
};

timeline.addEventListener("pointerdown", () => {
    dragging = true;
});

timeline.addEventListener("input", () => {
    seekInput.value = timeline.value;
});

timeline.addEventListener("change", () => {
    const target = Number(timeline.value);

    dragging = false;

    seekToPosition(target);
});

window.addEventListener("pointerup", () => {
    dragging = false;
});

window.addEventListener("pointercancel", () => {
    dragging = false;
});

$("mute").onchange = event => {
    video.muted = event.target.checked;
};

$("volume").oninput = event => {
    video.volume = Number(event.target.value);
};

video.addEventListener("loadedmetadata", () => {
    $("resolution").textContent =
        `${video.videoWidth} x ${video.videoHeight}`;

    synchronizePlayback();
    updateControls();
});

video.addEventListener("playing", () => {
    status("Playing");
    updateControls();
});

video.addEventListener("pause", () => {
    if (session?.playback.state === "PAUSED") {
        status("Paused");
    }

    updateControls();
});

video.addEventListener("waiting", () => {
    if (started) status("Buffering...");
});

video.addEventListener("timeupdate", updateMetrics);

video.addEventListener("error", () => {
    if (video.error) {
        status(`Video error: ${video.error.code}`);
    }
});

window.addEventListener("beforeunload", destroyPlayer);

function handleBackendEvent(event) {
    if (
        session &&
        event.generationId < session.generationId
    ) {
        return;
    }

    switch (event.type) {
        case "CHANNEL_STARTING":
            status("Changing channel...");
            break;

        case "CHANNEL_READY":
        case "PLAYBACK_READY":
            status("Channel ready");
            break;

        case "CHANNEL_FAILED":
        case "ERROR":
            status(
                event.payload?.message ??
                event.payload?.error?.message ??
                "Backend error"
            );
            break;

        case "RTP_SIGNAL_LOST":
            status("Signal lost");
            break;

        case "RTP_SIGNAL_RECOVERED":
            status("Signal recovered");
            break;

        case "BUFFER_POSITION_CLAMPED":
            status("Playback moved to available buffer");
            break;

        case "OPERATION_RESULT":
            if (event.payload?.error) {
                status(
                    event.payload.error.message ??
                    "Operation failed"
                );
            }
            break;
    }

    refreshSession();
}

function connectEvents() {
    if (shuttingDown || eventSocket) return;

    const socket = api.connectEvents();

    eventSocket = socket;

    socket.onopen = () => {
        if (eventSocket !== socket) return;

        console.log("[WebSocket] Connected");

        refreshSession();
    };

    socket.onmessage = message => {
        if (eventSocket !== socket) return;

        try {
            const event = JSON.parse(message.data);

            handleBackendEvent(event);

        } catch (error) {
            console.warn("[WebSocket]", error.message);
        }
    };

    socket.onerror = () => {
        console.warn("[WebSocket] Connection error");
    };

    socket.onclose = () => {
        if (eventSocket !== socket) return;

        eventSocket = null;

        if (shuttingDown) return;

        console.log("[WebSocket] Reconnecting...");

        reconnectTimer = setTimeout(connectEvents, 2000);
    };
}

function disconnectEvents() {
    shuttingDown = true;

    clearTimeout(reconnectTimer);

    const socket = eventSocket;

    eventSocket = null;

    if (socket) socket.close();
}

$("epg-button").onclick = () => {
    hideInfo();
    openGuide(services, session?.serviceId);
};
let channelDigits = "";
let channelInputTimer = null;

function clearChannelInput() {
    clearTimeout(channelInputTimer);

    channelDigits = "";
    $("channel-number").value = "";
}

function changeVolume(delta) {
    video.volume = clamp(video.volume + delta, 0, 1);

    $("volume").value = String(video.volume);
}

function seekRelative(deltaSeconds) {
    if (!started || !session?.playback.bufferAvailable) {
        return;
    }

    const offset = mediaOffset();

    if (offset === null) return;

    const playback = session.playback;

    const target = clamp(
        video.currentTime + offset + deltaSeconds,
        playback.earliestMs / 1000,
        playback.latestMs / 1000
    );

    seekToPosition(target);
}

function togglePause() {
    if (!started || !session?.playback.bufferAvailable) {
        return;
    }

    if (session.playback.state === "PAUSED") {
        playbackCommand(
            generationId => api.resume(generationId)
        );
    } else {
        playbackCommand(
            generationId => api.pause(generationId)
        );
    }
}

function toggleMute() {
    video.muted = !video.muted;

    $("mute").checked = video.muted;
}

function handleRemoteKey(event) {
    if (event.ctrlKey || event.altKey || event.metaKey) {
        return;
    }

    const key = event.key.toLowerCase();

    if (isGuideOpen()) {
        if (key === "escape" || key === "e") {
            event.preventDefault();
            closeGuide();
        }

        return;
    }

    if (key === "escape") {
        hideInfo();
        clearChannelInput();
        return;
    }

    if (
        event.target instanceof HTMLElement &&
        (
            event.target.matches("input, select, textarea") ||
            event.target.isContentEditable
        )
    ) {
        return;
    }

    if (
        event.repeat &&
        !["arrowleft", "arrowright", "+", "=", "-"].includes(key)
    ) {
        return;
    }

    if (/^[0-9]$/.test(key)) {
        event.preventDefault();

        if (channelDigits.length >= 3) {
            channelDigits = "";
        }

        channelDigits += key;

        $("channel-number").value = channelDigits;

        status(`Channel: ${channelDigits}`);

        clearTimeout(channelInputTimer);

        channelInputTimer = setTimeout(
            clearChannelInput,
            4000
        );

        return;
    }

    switch (key) {
        case "arrowup":
            changeChannel(1);
            break;

        case "arrowdown":
            changeChannel(-1);
            break;

        case "arrowleft":
            seekRelative(-10);
            break;

        case "arrowright":
            seekRelative(10);
            break;

        case "enter":
            if (channelDigits !== "") {
                selectChannelNumber();
                clearChannelInput();
            } else {
                return;
            }
            break;

        case " ":
            if (started) {
                togglePause();
            } else {
                startPlayback();
            }
            break;

        case "+":
        case "=":
            changeVolume(0.05);
            break;

        case "-":
            changeVolume(-0.05);
            break;

        case "m":
            toggleMute();
            break;

        case "i":
            if (started && session?.activeService) {
                toggleInfo(session.activeService);
            }
            break;

        case "e":
            hideInfo();
            openGuide(services, session?.serviceId);
            break;

        case "l":
            if (started) {
                playbackCommand(
                    generationId => api.goLive(generationId)
                );
            }
            break;

        case "backspace":
            if (started && session?.playback.bufferAvailable) {
                seekToPosition(
                    session.playback.earliestMs / 1000
                );
            }
            break;

        default:
            return;
    }

    event.preventDefault();
}

window.addEventListener("keydown", handleRemoteKey);

async function initialize() {
    updateControls();

    try {
        await loadServices();
        await refreshSession();

        connectEvents();

    } catch (error) {
        status(error.message);
    }
}

window.addEventListener("beforeunload", disconnectEvents);

initialize();

setInterval(refreshSession, 2000);
setInterval(sendPlaybackFeedback, 1000);
