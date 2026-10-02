#pragma once

// Launches the Core service wrapper through a detached helper. The wrapper is
// an idempotent ensure: it exits without replacing an already-running Core.
void zaparoo_service_start_async(void);
