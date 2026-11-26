import test from "node:test";
import assert from "node:assert/strict";

import {
    mkdtempSync,
    writeFileSync,
    rmSync,
    readFileSync
} from "node:fs";

import { tmpdir } from "node:os";
import { join } from "node:path";

import { ServiceRepository } from "../src/serviceRepository.js";
import { EpgRepository } from "../src/epgRepository.js";

const services = new ServiceRepository();

function validServices() {
    return services.getAll();
}

function validEpg() {
    return JSON.parse(
        readFileSync(
            new URL("../data/epg.json", import.meta.url),
            "utf8"
        )
    );
}

function withTemporaryFile(content, callback) {
    const directory = mkdtempSync(
        join(tmpdir(), "iptv-validation-")
    );

    const filePath = join(directory, "data.json");

    try {
        writeFileSync(filePath, content, "utf8");

        return callback(filePath);
    } finally {
        rmSync(directory, {
            recursive: true,
            force: true
        });
    }
}

function rejectServices(data, expectedError) {
    withTemporaryFile(
        JSON.stringify(data),
        filePath => {
            assert.throws(
                () => new ServiceRepository(filePath),
                expectedError
            );
        }
    );
}

function rejectEpg(data, expectedError) {
    withTemporaryFile(
        JSON.stringify(data),
        filePath => {
            assert.throws(
                () => new EpgRepository(
                    services,
                    filePath
                ),
                expectedError
            );
        }
    );
}

// Service catalog validation

test("Reject malformed service JSON", () => {
    withTemporaryFile("{invalid", filePath => {
        assert.throws(
            () => new ServiceRepository(filePath),
            SyntaxError
        );
    });
});

test("Reject non-array service catalog", () => {
    rejectServices({}, /must be an array/);
});

test("Reject empty service catalog", () => {
    rejectServices([], /catalog is empty/);
});

test("Reject null service object", () => {
    rejectServices([null], /Invalid service object/);
});

test("Reject duplicate service ID", () => {
    const data = validServices();

    data[1].serviceId = data[0].serviceId;

    rejectServices(data, /Duplicate service ID/);
});

test("Reject duplicate channel number", () => {
    const data = validServices();

    data[1].channelNumber = data[0].channelNumber;

    rejectServices(data, /Duplicate channel number/);
});

test("Reject invalid service IDs", () => {
    for (const value of [0, -1, 65536, 1.5, "1"]) {
        const data = validServices();

        data[0].serviceId = value;

        rejectServices(data, /Invalid service ID/);
    }
});

test("Reject invalid channel numbers", () => {
    for (const value of [0, -1, 1.5, "1", 4294967296]) {
        const data = validServices();

        data[0].channelNumber = value;

        rejectServices(data, /Invalid channel number/);
    }
});

test("Reject empty service name", () => {
    const data = validServices();

    data[0].name = "   ";

    rejectServices(data, /Invalid service name/);
});

test("Reject invalid service description", () => {
    const data = validServices();

    data[0].description = null;

    rejectServices(data, /Invalid service description/);
});

test("Reject missing stream profile", () => {
    const data = validServices();

    delete data[0].streamProfile;

    rejectServices(data, /Invalid stream profile/);
});

test("Reject unsupported stream profile", () => {
    const fields = [
        "protocol",
        "container",
        "videoCodec",
        "audioCodec"
    ];

    for (const field of fields) {
        const data = validServices();

        data[0].streamProfile[field] = "INVALID";

        rejectServices(data, /Invalid stream profile/);
    }
});

test("Reject empty RTP address", () => {
    const data = validServices();

    data[0].streamProfile.rtpAddress = "";

    rejectServices(data, /Invalid RTP address/);
});

test("Reject invalid RTP ports", () => {
    for (const value of [0, -1, 65536, 1.5, "5004"]) {
        const data = validServices();

        data[0].streamProfile.rtpPort = value;

        rejectServices(data, /Invalid RTP port/);
    }
});

test("Sort catalog by channel number", () => {
    const data = validServices().reverse();

    withTemporaryFile(
        JSON.stringify(data),
        filePath => {
            const repository =
                new ServiceRepository(filePath);

            assert.deepEqual(
                repository.getAll().map(
                    service => service.channelNumber
                ),
                [1, 2, 3]
            );
        }
    );
});

// EPG validation

test("Reject malformed EPG JSON", () => {
    withTemporaryFile("{invalid", filePath => {
        assert.throws(
            () => new EpgRepository(services, filePath),
            SyntaxError
        );
    });
});

test("Reject non-array EPG templates", () => {
    rejectEpg({}, /must be an array/);
});

test("Reject null EPG template", () => {
    rejectEpg([null], /Invalid EPG template/);
});

test("Reject unknown EPG service ID", () => {
    const data = validEpg();

    data[0].serviceId = 99;

    rejectEpg(data, /Unknown EPG service ID/);
});

test("Reject duplicate EPG service ID", () => {
    const data = validEpg();

    data[1].serviceId = data[0].serviceId;

    rejectEpg(data, /Duplicate EPG service ID/);
});

test("Reject empty program template", () => {
    const data = validEpg();

    data[0].programs = [];

    rejectEpg(data, /Empty EPG program template/);
});

test("Reject invalid program collection", () => {
    const data = validEpg();

    data[0].programs = null;

    rejectEpg(data, /Empty EPG program template/);
});

test("Reject null EPG program", () => {
    const data = validEpg();

    data[0].programs[0] = null;

    rejectEpg(data, /Invalid EPG program/);
});

test("Reject empty EPG title", () => {
    const data = validEpg();

    data[0].programs[0].title = " ";

    rejectEpg(data, /Invalid EPG title/);
});

test("Reject invalid EPG description", () => {
    const data = validEpg();

    data[0].programs[0].description = null;

    rejectEpg(data, /Invalid EPG description/);
});

test("Reject invalid program durations", () => {
    for (const value of [0, -1, 1441, 1.5, "60"]) {
        const data = validEpg();

        data[0].programs[0].durationMinutes = value;

        rejectEpg(data, /Invalid EPG duration/);
    }
});

test("Service without EPG data", () => {
    const data = validEpg().filter(
        item => item.serviceId !== 3
    );

    withTemporaryFile(
        JSON.stringify(data),
        filePath => {
            const repository =
                new EpgRepository(services, filePath);

            assert.deepEqual(
                repository.getSchedule(3, "2026-11-26"),
                []
            );

            assert.equal(
                repository.getCurrentEvent(3),
                null
            );
        }
    );
});

test("Empty EPG catalog is supported", () => {
    withTemporaryFile("[]", filePath => {
        const repository =
            new EpgRepository(services, filePath);

        for (const service of services.getAll()) {
            assert.deepEqual(
                repository.getSchedule(
                    service.serviceId,
                    "2026-11-26"
                ),
                []
            );
        }
    });
});

test("Last EPG event is limited to day boundary", () => {
    const data = validEpg();

    data[0].programs = [
        {
            title: "Test program",
            description: "Boundary test",
            durationMinutes: 100
        }
    ];

    withTemporaryFile(
        JSON.stringify(data),
        filePath => {
            const repository =
                new EpgRepository(services, filePath);

            const events = repository.getSchedule(
                1,
                "2026-11-26"
            );

            assert.equal(events.length, 15);

            assert.equal(
                events.at(-1).endTime,
                "2026-11-27T00:00:00.000Z"
            );

            const duration =
                Date.parse(events.at(-1).endTime) -
                Date.parse(events.at(-1).startTime);

            assert.equal(duration, 40 * 60 * 1000);
        }
    );
});
