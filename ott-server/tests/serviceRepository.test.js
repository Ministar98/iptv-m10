import test from "node:test";
import assert from "node:assert/strict";

import { ServiceRepository } from "../src/serviceRepository.js";

test("Load service catalog", () => {
    const repository = new ServiceRepository();

    assert.equal(repository.count, 3);
});

test("Get all services", () => {
    const repository = new ServiceRepository();

    const services = repository.getAll();

    assert.deepEqual(
        services.map(service => service.serviceId),
        [1, 2, 3]
    );
});

test("Find service by ID", () => {
    const repository = new ServiceRepository();

    const service = repository.getById(2);

    assert.equal(service.name, "Channel 2");
    assert.equal(service.channelNumber, 2);
});

test("Find service by channel number", () => {
    const repository = new ServiceRepository();

    const service = repository.getByChannelNumber(3);

    assert.equal(service.serviceId, 3);
});

test("Unknown service ID", () => {
    const repository = new ServiceRepository();

    assert.equal(repository.getById(99), null);
});

test("Unknown channel number", () => {
    const repository = new ServiceRepository();

    assert.equal(
        repository.getByChannelNumber(99),
        null
    );
});

test("Catalog data isolation", () => {
    const repository = new ServiceRepository();

    const services = repository.getAll();

    services[0].name = "Modified";
    services[0].streamProfile.rtpPort = 9999;

    const original = repository.getById(1);

    assert.equal(original.name, "Channel 1");
    assert.equal(original.streamProfile.rtpPort, 5004);
});

test("Stream profile validation", () => {
    const repository = new ServiceRepository();

    for (const service of repository.getAll()) {
        assert.equal(
            service.streamProfile.protocol,
            "RTP"
        );

        assert.equal(
            service.streamProfile.container,
            "MPEG-TS"
        );

        assert.equal(
            service.streamProfile.rtpPort,
            5004
        );
    }
});
