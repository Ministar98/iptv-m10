import { loadEpg, formatTime } from "./epg.js";

const guide = document.createElement("dialog");

guide.id = "epg-guide";

guide.innerHTML = `
    <div class="guide-header">
        <h2>EPG / TV Guide</h2>
        <button id="guide-close">Close</button>
    </div>

    <select id="guide-service"></select>

    <p id="guide-status"></p>

    <div class="guide-program">
        <table>
            <thead>
                <tr>
                    <th>Start</th>
                    <th>End</th>
                    <th>Program</th>
                </tr>
            </thead>
            <tbody id="guide-events"></tbody>
        </table>
    </div>
`;

document.body.appendChild(guide);

const $ = id => document.getElementById(id);

let requestRevision = 0;

async function refreshGuide() {
    const revision = ++requestRevision;
    const serviceId = Number($("guide-service").value);
    const body = $("guide-events");

    body.replaceChildren();

    $("guide-status").textContent = "Loading...";

    try {
        const result = await loadEpg(serviceId);

        if (revision !== requestRevision || !guide.open) {
            return;
        }

        const now = Date.now();

        $("guide-status").textContent = result.stale
            ? `${result.date} - Cached EPG`
            : result.date;

        if (result.events.length === 0) {
            $("guide-status").textContent =
                "Podaci nisu dostupni";
        }

        for (const event of result.events) {
            const row = document.createElement("tr");

            if (
                Date.parse(event.startTime) <= now &&
                now < Date.parse(event.endTime)
            ) {
                row.className = "current-program";
            }

            const values = [
                formatTime(event.startTime),
                formatTime(event.endTime),
                event.title
            ];

            for (const value of values) {
                const cell = document.createElement("td");

                cell.textContent = value;
                row.appendChild(cell);
            }

            body.appendChild(row);
        }

        body.querySelector(".current-program")
            ?.scrollIntoView({ block: "nearest" });

    } catch {
        if (revision !== requestRevision) return;

        $("guide-status").textContent =
            "Podaci nisu dostupni";
    }
}

export function openGuide(services, activeServiceId) {
    if (guide.open || services.length === 0) return;

    const select = $("guide-service");

    select.replaceChildren();

    for (const service of services) {
        const option = document.createElement("option");

        option.value = String(service.serviceId);

        option.textContent =
            `${service.channelNumber} - ${service.name}`;

        select.appendChild(option);
    }

    if (services.some(s => s.serviceId === activeServiceId)) {
        select.value = String(activeServiceId);
    }

    guide.showModal();

    refreshGuide();
}

export function closeGuide() {
    requestRevision++;

    if (guide.open) guide.close();
}

export function isGuideOpen() {
    return guide.open;
}

$("guide-close").onclick = closeGuide;
$("guide-service").onchange = refreshGuide;

guide.addEventListener("close", () => {
    requestRevision++;
});
