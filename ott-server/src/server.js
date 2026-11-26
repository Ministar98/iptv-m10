import { createApp } from "./app.js";

const HOST = "127.0.0.1";
const PORT = 8090;

function main() {
    const app = createApp();

    const server = app.listen(PORT, HOST, () => {
        console.log("================================");
        console.log("IPTV M10 OTT Middleware");
        console.log(`Listening on ${HOST}:${PORT}`);
        console.log("================================");
    });

    server.requestTimeout = 10000;
    server.headersTimeout = 5000;
    server.keepAliveTimeout = 5000;

    server.on("error", error => {
        console.error("[ERROR]", error.message);

        process.exitCode = 1;
    });

    let stopping = false;

    function shutdown() {
        if (stopping) {
            return;
        }

        stopping = true;

        console.log("[INFO] Stopping OTT server");

        const timeout = setTimeout(() => {
            server.closeAllConnections();
        }, 3000);

        timeout.unref();

        server.close(error => {
            clearTimeout(timeout);

            if (error) {
                console.error("[ERROR]", error.message);

                process.exitCode = 1;

                return;
            }

            console.log("[INFO] OTT server stopped");
        });
    }

    process.once("SIGINT", shutdown);
    process.once("SIGTERM", shutdown);
}

try {
    main();
} catch (error) {
    console.error(
        "[ERROR] OTT startup failed:",
        error.message
    );

    process.exitCode = 1;
}
