import { loadNowNext, formatTime } from "./epg.js";

let hideTimer = null;
let requestRevision = 0;

const osd = document.createElement("div");

osd.id = "info-osd";
osd.hidden = true;

osd.innerHTML = `
    <div id="info-channel"></div>
    <div id="info-current"></div>
    <div id="info-next"></div>
`;

const video = document.getElementById("video");
const container = document.createElement("div");

container.className = "video-container";

video.before(container);
container.append(video, osd);

const $ = id => document.getElementById(id);

function formatEvent(event) {
    if (!event) return "Podaci nisu dostupni";

    return `${event.title} | ` +
           `${formatTime(event.startTime)} - ` +
           `${formatTime(event.endTime)}`;
}

export function hideInfo() {
    clearTimeout(hideTimer);
    requestRevision++;
    osd.hidden = true;
}

export async function showInfo(service) {
    if (!service) return;

    const revision = ++requestRevision;

    clearTimeout(hideTimer);

    $("info-channel").textContent =
        `${service.channelNumber} - ${service.name}`;

    $("info-current").textContent = "";
    $("info-next").textContent = "";

    osd.hidden = false;

    hideTimer = setTimeout(hideInfo, 3000);

    try {
        const result = await loadNowNext(service.serviceId);

        if (revision !== requestRevision) return;

        $("info-current").textContent =
            `Sada: ${formatEvent(result.current)}`;

        $("info-next").textContent =
            `Sledeće: ${formatEvent(result.next)}`;

        if (result.stale) {
            $("info-channel").textContent += " (Keširani EPG)";
        }

    } catch {
        if (revision !== requestRevision) return;

        $("info-current").textContent =
            "EPG: Podaci nisu dostupni";
    }
}

export function toggleInfo(service) {
    if (osd.hidden) {
        showInfo(service);
    } else {
        hideInfo();
    }
}
