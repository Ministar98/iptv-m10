import { readFileSync } from "node:fs";

const MINUTE_MS = 60 * 1000;
const DAY_MS = 24 * 60 * MINUTE_MS;

export class EpgRepository {
    #templates;
    #serviceRepository;

    constructor(
        serviceRepository,
        filePath = new URL(
            "../data/epg.json",
            import.meta.url
        )
    ) {
        this.#serviceRepository = serviceRepository;

        const content = readFileSync(filePath, "utf8");

        this.#templates = JSON.parse(content);

        this.#validate();
    }

    static isValidDate(date) {
        if (
            typeof date !== "string" ||
            !/^\d{4}-\d{2}-\d{2}$/.test(date)
        ) {
            return false;
        }

        const parsed = new Date(
            `${date}T00:00:00.000Z`
        );

        return (
            Number.isFinite(parsed.getTime()) &&
            parsed.toISOString().slice(0, 10) === date
        );
    }

    static currentDate(now = new Date()) {
        return now.toISOString().slice(0, 10);
    }

    getSchedule(
        serviceId,
        date = EpgRepository.currentDate()
    ) {
        if (!this.#serviceRepository.getById(serviceId)) {
            return null;
        }

        if (!EpgRepository.isValidDate(date)) {
            throw new RangeError("Invalid EPG date");
        }

        const template = this.#templates.find(
            item => item.serviceId === serviceId
        );

        if (!template) {
            return [];
        }

        const dayStart = Date.parse(
            `${date}T00:00:00.000Z`
        );

        const dayEnd = dayStart + DAY_MS;

        const events = [];

        let position = dayStart;
        let index = 0;

        while (position < dayEnd) {
            const program =
                template.programs[
                    index % template.programs.length
                ];

            const end = Math.min(
                position +
                    program.durationMinutes * MINUTE_MS,
                dayEnd
            );

            events.push({
                eventId: `${serviceId}-${date}-${index + 1}`,
                serviceId,
                title: program.title,
                description: program.description,
                startTime: new Date(position).toISOString(),
                endTime: new Date(end).toISOString()
            });

            position = end;
            index++;
        }

        return events;
    }

    getCurrentEvent(
        serviceId,
        now = new Date()
    ) {
        const date = EpgRepository.currentDate(now);

        const schedule = this.getSchedule(
            serviceId,
            date
        );

        if (!schedule) {
            return null;
        }

        const timestamp = now.getTime();

        return schedule.find(event =>
            Date.parse(event.startTime) <= timestamp &&
            timestamp < Date.parse(event.endTime)
        ) ?? null;
    }

    #validate() {
        if (!Array.isArray(this.#templates)) {
            throw new Error(
                "EPG templates must be an array"
            );
        }

        const serviceIds = new Set();

        for (const template of this.#templates) {
            if (!template || typeof template !== "object") {
                throw new Error("Invalid EPG template");
            }

            if (
                !this.#serviceRepository.getById(
                    template.serviceId
                )
            ) {
                throw new Error("Unknown EPG service ID");
            }

            if (serviceIds.has(template.serviceId)) {
                throw new Error("Duplicate EPG service ID");
            }

            if (
                !Array.isArray(template.programs) ||
                template.programs.length === 0
            ) {
                throw new Error("Empty EPG program template");
            }

            for (const program of template.programs) {
                if (!program || typeof program !== "object") {
                    throw new Error("Invalid EPG program");
                }

                if (
                    typeof program.title !== "string" ||
                    program.title.trim().length === 0
                ) {
                    throw new Error("Invalid EPG title");
                }

                if (
                    typeof program.description !== "string"
                ) {
                    throw new Error("Invalid EPG description");
                }

                if (
                    !Number.isInteger(program.durationMinutes) ||
                    program.durationMinutes <= 0 ||
                    program.durationMinutes > 1440
                ) {
                    throw new Error("Invalid EPG duration");
                }
            }

            serviceIds.add(template.serviceId);
        }
    }
}
