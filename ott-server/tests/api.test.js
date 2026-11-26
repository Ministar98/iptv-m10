import test, {
    before,
    after
} from "node:test";

import assert from "node:assert/strict";

import { once } from "node:events";

import { createApp } from "../src/app.js";

import { ServiceRepository } from "../src/serviceRepository.js";

let server;
let baseUrl;

before(async () => {
    server = createApp().listen(0, "127.0.0.1");

    await once(server, "listening");

    baseUrl =
        `http://127.0.0.1:${server.address().port}`;
});

after(async () => {
    if (server) {
        await new Promise((resolve, reject) => {
            server.close(error => {
                if (error) {
                    reject(error);
                } else {
                    resolve();
                }
            });
        });
    }
});

async function request(path) {
    const response = await fetch(
        `${baseUrl}${path}`,
        {
            signal: AbortSignal.timeout(5000)
        }
    );

    return {
        status: response.status,
        headers: response.headers,
        body: await response.json()
    };
}

test("Health endpoint", async () => {
    const result = await request("/ott/v1/health");

    assert.equal(result.status, 200);
    assert.equal(result.body.status, "OK");
    assert.equal(result.body.serviceCount, 3);
});

test("Service catalog endpoint", async () => {
    const result = await request("/ott/v1/services");

    assert.equal(result.status, 200);
    assert.equal(result.body.length, 3);

    assert.deepEqual(
        result.body.map(service => service.serviceId),
        [1, 2, 3]
    );
});

test("Service catalog matches repository", async () => {
    const result = await request("/ott/v1/services");

    const repository = new ServiceRepository();

    assert.deepEqual(
        result.body,
        repository.getAll()
    );
});

test("EPG endpoint for all services", async () => {
    for (const serviceId of [1, 2, 3]) {
        const result = await request(
            `/ott/v1/epg?serviceId=${serviceId}` +
            "&date=2026-11-26"
        );

        assert.equal(result.status, 200);
        assert.ok(result.body.length > 0);

        for (const event of result.body) {
            assert.equal(event.serviceId, serviceId);

            assert.ok(
                Date.parse(event.endTime) >
                Date.parse(event.startTime)
            );
        }
    }
});

test("EPG covers requested UTC day", async () => {
    const result = await request(
        "/ott/v1/epg?serviceId=1&date=2026-11-26"
    );

    assert.equal(result.status, 200);

    assert.equal(
        result.body[0].startTime,
        "2026-11-26T00:00:00.000Z"
    );

    assert.equal(
        result.body.at(-1).endTime,
        "2026-11-27T00:00:00.000Z"
    );
});

test("EPG defaults to current date", async () => {
    const beforeDate =
        new Date().toISOString().slice(0, 10);

    const result = await request(
        "/ott/v1/epg?serviceId=1"
    );

    const afterDate =
        new Date().toISOString().slice(0, 10);

    assert.equal(result.status, 200);

    const actualDate =
        result.body[0].startTime.slice(0, 10);

    assert.ok(
        [beforeDate, afterDate].includes(actualDate)
    );
});

test("Missing service ID", async () => {
    const result = await request("/ott/v1/epg");

    assert.equal(result.status, 400);

    assert.equal(
        result.body.error.code,
        "INVALID_SERVICE_ID"
    );
});

test("Invalid service IDs", async () => {
    for (const value of [
        "0",
        "-1",
        "abc",
        "1.5",
        "65536",
        ""
    ]) {
        const result = await request(
            `/ott/v1/epg?serviceId=${value}`
        );

        assert.equal(result.status, 400);

        assert.equal(
            result.body.error.code,
            "INVALID_SERVICE_ID"
        );
    }
});

test("Duplicate service ID parameter", async () => {
    const result = await request(
        "/ott/v1/epg?serviceId=1&serviceId=2"
    );

    assert.equal(result.status, 400);
});

test("Unknown service ID", async () => {
    const result = await request(
        "/ott/v1/epg?serviceId=99"
    );

    assert.equal(result.status, 404);

    assert.equal(
        result.body.error.code,
        "SERVICE_NOT_FOUND"
    );
});

test("Invalid EPG dates", async () => {
    for (const date of [
        "2026-02-30",
        "26-11-2026",
        "invalid",
        ""
    ]) {
        const result = await request(
            `/ott/v1/epg?serviceId=1&date=${date}`
        );

        assert.equal(result.status, 400);

        assert.equal(
            result.body.error.code,
            "INVALID_DATE"
        );
    }
});

test("Duplicate date parameter", async () => {
    const result = await request(
        "/ott/v1/epg?serviceId=1" +
        "&date=2026-11-26&date=2026-11-27"
    );

    assert.equal(result.status, 400);
});

test("Unknown endpoint", async () => {
    const result = await request("/ott/v1/unknown");

    assert.equal(result.status, 404);

    assert.equal(
        result.body.error.code,
        "ENDPOINT_NOT_FOUND"
    );
});

test("JSON response and cache headers", async () => {
    const result = await request("/ott/v1/services");

    assert.match(
        result.headers.get("content-type"),
        /application\/json/
    );

    assert.equal(
        result.headers.get("cache-control"),
        "no-store"
    );
});

test("Concurrent REST requests", async () => {
    const requests = Array.from(
        { length: 30 },
        (_, index) => request(
            `/ott/v1/epg?serviceId=${index % 3 + 1}`
        )
    );

    const results = await Promise.all(requests);

    for (const result of results) {
        assert.equal(result.status, 200);
        assert.ok(result.body.length > 0);
    }
});
