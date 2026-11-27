import { api } from "./api.js";

export function getDateKey(date = new Date()) {
    return date.toISOString().slice(0, 10);
}

export function formatTime(value) {
    return new Date(value).toLocaleTimeString("sr-RS", {
        hour: "2-digit",
        minute: "2-digit"
    });
}

export async function loadEpg(
    serviceId,
    date = getDateKey()
) {
    const result = await api.getEpg(serviceId, date);

    return {
        serviceId,
        date,
        stale: result.stale ?? false,
        events: [...result.events].sort(
            (a, b) =>
                Date.parse(a.startTime) -
                Date.parse(b.startTime)
        )
    };
}

export function getNowNext(events, now = Date.now()) {
    const current = events.find(event =>
        Date.parse(event.startTime) <= now &&
        now < Date.parse(event.endTime)
    ) ?? null;

    const next = events.find(event =>
        Date.parse(event.startTime) > now
    ) ?? null;

    return {
        current,
        next
    };
}

export async function loadNowNext(serviceId) {
    const now = Date.now();

    const today = await loadEpg(
        serviceId,
        getDateKey(new Date(now))
    );

    let { current, next } = getNowNext(
        today.events,
        now
    );

    if (!next) {
        const tomorrow = await loadEpg(
            serviceId,
            getDateKey(new Date(now + 86400000))
        );

        next = tomorrow.events[0] ?? null;
    }

    return {
        serviceId,
        current,
        next,
        stale: today.stale
    };
}
