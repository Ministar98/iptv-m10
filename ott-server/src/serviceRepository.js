import { readFileSync } from "node:fs";

export class ServiceRepository {
    #services;

    constructor(
        filePath = new URL(
            "../data/services.json",
            import.meta.url
        )
    ) {
        const content = readFileSync(filePath, "utf8");

        const services = JSON.parse(content);

        this.#validate(services);

        this.#services = services.sort(
            (a, b) => a.channelNumber - b.channelNumber
        );
    }

    getAll() {
        return structuredClone(this.#services);
    }

    getById(serviceId) {
        const service = this.#services.find(
            item => item.serviceId === serviceId
        );

        return service ? structuredClone(service) : null;
    }

    getByChannelNumber(channelNumber) {
        const service = this.#services.find(
            item => item.channelNumber === channelNumber
        );

        return service ? structuredClone(service) : null;
    }

    get count() {
        return this.#services.length;
    }

    #validate(services) {
        if (!Array.isArray(services)) {
            throw new Error(
                "Service catalog must be an array"
            );
        }

        if (services.length === 0) {
            throw new Error(
                "Service catalog is empty"
            );
        }

        const serviceIds = new Set();
        const channelNumbers = new Set();

        for (const service of services) {
            if (!service || typeof service !== "object") {
                throw new Error("Invalid service object");
            }

            if (
                !Number.isInteger(service.serviceId) ||
                service.serviceId <= 0 ||
                service.serviceId > 65535
            ) {
                throw new Error("Invalid service ID");
            }

            if (
                !Number.isSafeInteger(service.channelNumber) ||
                service.channelNumber <= 0 ||
                service.channelNumber > 4294967295
            ) {
                throw new Error("Invalid channel number");
            }

            if (
                typeof service.name !== "string" ||
                service.name.trim().length === 0
            ) {
                throw new Error("Invalid service name");
            }

            if (typeof service.description !== "string") {
                throw new Error("Invalid service description");
            }

            if (serviceIds.has(service.serviceId)) {
                throw new Error("Duplicate service ID");
            }

            if (channelNumbers.has(service.channelNumber)) {
                throw new Error("Duplicate channel number");
            }

            const profile = service.streamProfile;

            if (
                !profile ||
                profile.protocol !== "RTP" ||
                profile.container !== "MPEG-TS" ||
                profile.videoCodec !== "H.264" ||
                profile.audioCodec !== "AAC"
            ) {
                throw new Error("Invalid stream profile");
            }

            if (
                typeof profile.rtpAddress !== "string" ||
                profile.rtpAddress.trim().length === 0
            ) {
                throw new Error("Invalid RTP address");
            }

            if (
                !Number.isInteger(profile.rtpPort) ||
                profile.rtpPort <= 0 ||
                profile.rtpPort > 65535
            ) {
                throw new Error("Invalid RTP port");
            }

            serviceIds.add(service.serviceId);
            channelNumbers.add(service.channelNumber);
        }
    }
}
