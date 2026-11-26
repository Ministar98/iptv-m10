import test from "node:test";
import assert from "node:assert/strict";

import { ServiceRepository } from "../src/serviceRepository.js";
import { EpgRepository } from "../src/epgRepository.js";

const services = new ServiceRepository();
const epg = new EpgRepository(services);

const DATE = "2026-11-26";

test("Generate EPG for all services", () => {
    for (const service of services.getAll()) {
        const events = epg.getSchedule(
            service.serviceId,
            DATE
        );

        assert.ok(events.length > 0);

        for (const event of events) {
            assert.equal(
                event.serviceId,
                service.serviceId
            );
        }
    }
});

test("Full 24-hour schedule", () => {
    for (const service of services.getAll()) {
        const events = epg.getSchedule(
            service.serviceId,
            DATE
        );

        assert.equal(
            events[0].startTime,
            "2026-11-26T00:00:00.000Z"
        );

        assert.equal(
            events.at(-1).endTime,
            "2026-11-27T00:00:00.000Z"
        );
    }
});

test("No schedule gaps or overlaps", () => {
    for (const service of services.getAll()) {
        const events = epg.getSchedule(
            service.serviceId,
            DATE
        );

        for (let i = 1; i < events.length; i++) {
            assert.equal(
                events[i - 1].endTime,
                events[i].startTime
            );
        }
    }
});

test("Positive event durations", () => {
    for (const event of epg.getSchedule(1, DATE)) {
        assert.ok(
            Date.parse(event.endTime) >
            Date.parse(event.startTime)
        );
    }
});

test("Unique event IDs", () => {
    const ids = [];

    for (const service of services.getAll()) {
        const events = epg.getSchedule(
            service.serviceId,
            DATE
        );

        ids.push(
            ...events.map(event => event.eventId)
        );
    }

    assert.equal(new Set(ids).size, ids.length);
});

test("Deterministic schedule", () => {
    assert.deepEqual(
        epg.getSchedule(1, DATE),
        epg.getSchedule(1, DATE)
    );
});

test("Different channel schedules", () => {
    const first = epg.getSchedule(1, DATE);
    const second = epg.getSchedule(2, DATE);

    assert.notEqual(
        first[0].title,
        second[0].title
    );
});

test("Unknown service", () => {
    assert.equal(
        epg.getSchedule(99, DATE),
        null
    );
});

test("Invalid date format", () => {
    assert.throws(
        () => epg.getSchedule(1, "26-11-2026"),
        RangeError
    );
});

test("Invalid calendar date", () => {
    assert.throws(
        () => epg.getSchedule(1, "2026-02-30"),
        RangeError
    );
});

test("Leap year validation", () => {
    assert.equal(
        EpgRepository.isValidDate("2028-02-29"),
        true
    );

    assert.equal(
        EpgRepository.isValidDate("2026-02-29"),
        false
    );
});

test("Current event detection", () => {
    const now = new Date(
        "2026-11-26T00:30:00.000Z"
    );

    const event = epg.getCurrentEvent(1, now);

    assert.equal(event.title, "Morning News");
});

test("Exact event boundary", () => {
    const now = new Date(
        "2026-11-26T01:00:00.000Z"
    );

    const event = epg.getCurrentEvent(1, now);

    assert.equal(event.title, "Daily Report");
});

test("Current event available during demonstration", () => {
    for (const service of services.getAll()) {
        assert.ok(
            epg.getCurrentEvent(service.serviceId)
        );
    }
});

test("Generated schedule isolation", () => {
    const events = epg.getSchedule(1, DATE);

    events[0].title = "Modified";

    assert.equal(
        epg.getSchedule(1, DATE)[0].title,
        "Morning News"
    );
});
