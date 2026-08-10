#pragma once

#include "InventoryIpcServer.h"

#include <memory>

class InventoryIpcController {
public:
    InventoryIpcController();
    ~InventoryIpcController();

    InventoryIpcController(const InventoryIpcController&) = delete;
    InventoryIpcController& operator=(const InventoryIpcController&) = delete;

    bool Start();
    void Stop();
    void Pump();
    InventoryIpcServerStatus GetStatus() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
