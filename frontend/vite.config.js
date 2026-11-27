import { defineConfig } from "vite";

const backend = "http://127.0.0.1:8080";

const proxy = {
    "/api/v1/": {
        target: backend,
        changeOrigin: true,
        ws: true
    },
    "/hls/": {
        target: backend,
        changeOrigin: true
    }
};

export default defineConfig({
    server: {
        host: "127.0.0.1",
        port: 5173,
        strictPort: true,
        proxy
    },
    preview: {
        host: "127.0.0.1",
        port: 5173,
        strictPort: true,
        proxy
    }
});
