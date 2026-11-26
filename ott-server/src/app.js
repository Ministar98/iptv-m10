import express from "express";

import { ServiceRepository } from "./serviceRepository.js";
import { EpgRepository } from "./epgRepository.js";

function sendError(res, status, code, message) {
    return res.status(status).json({
        error: {
            code,
            message
        }
    });
}

function parseServiceId(value) {
    if (
        typeof value !== "string" ||
        !/^[1-9]\d{0,4}$/.test(value)
    ) {
        return null;
    }

    const serviceId = Number(value);

    if (serviceId > 65535) {
        return null;
    }

    return serviceId;
}

export function createApp(
    serviceRepository = new ServiceRepository(),
    epgRepository = new EpgRepository(serviceRepository)
) {
    const app = express();

    app.disable("x-powered-by");

    app.use(express.json({
        limit: "16kb"
    }));

    app.use((req, res, next) => {
        res.setHeader("Cache-Control", "no-store");

        next();
    });

    app.get("/ott/v1/health", (req, res) => {
        res.status(200).json({
            status: "OK",
            service: "IPTV M10 OTT Middleware",
            serviceCount: serviceRepository.count
        });
    });

    app.get("/ott/v1/services", (req, res) => {
        res.status(200).json(
            serviceRepository.getAll()
        );
    });

    app.get("/ott/v1/epg", (req, res) => {
        const serviceId = parseServiceId(
            req.query.serviceId
        );

        if (serviceId === null) {
            return sendError(
                res,
                400,
                "INVALID_SERVICE_ID",
                "A valid serviceId is required"
            );
        }

        const date =
            req.query.date ??
            EpgRepository.currentDate();

        if (!EpgRepository.isValidDate(date)) {
            return sendError(
                res,
                400,
                "INVALID_DATE",
                "Date must be a valid YYYY-MM-DD value"
            );
        }

        const schedule = epgRepository.getSchedule(
            serviceId,
            date
        );

        if (schedule === null) {
            return sendError(
                res,
                404,
                "SERVICE_NOT_FOUND",
                "Requested service does not exist"
            );
        }

        return res.status(200).json(schedule);
    });

    app.use((req, res) => {
        return sendError(
            res,
            404,
            "ENDPOINT_NOT_FOUND",
            "Requested endpoint does not exist"
        );
    });

    app.use((error, req, res, next) => {
        if (res.headersSent) {
            return next(error);
        }

        if (error.type === "entity.parse.failed") {
            return sendError(
                res,
                400,
                "INVALID_JSON",
                "Request contains invalid JSON"
            );
        }

        if (error.type === "entity.too.large") {
            return sendError(
                res,
                413,
                "PAYLOAD_TOO_LARGE",
                "Request body exceeds the allowed size"
            );
        }

        console.error("[ERROR]", error.message);

        return sendError(
            res,
            500,
            "INTERNAL_ERROR",
            "Internal OTT server error"
        );
    });

    return app;
}
