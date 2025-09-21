OpenAPI docs

This folder contains the OpenAPI YAML and a simple Swagger UI HTML page.

To view locally (recommended to avoid CORS issues):

1. Start a simple HTTP server from the `docs/` directory:

```bash
cd docs
python3 -m http.server 8000
```

2. Open your browser to `http://localhost:8000/openapi_notification.html`.

The page uses the Swagger UI CDN and will load `openapi_notification.yaml` from the same folder.