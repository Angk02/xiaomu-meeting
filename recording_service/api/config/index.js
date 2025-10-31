"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.configs = void 0;
const configs = {
    loglevel: "debug",
    ip: process.env.POD_IP,
    port: 3000,
    qps: 0,
    qlCoefficient: 0,
    service: "api",
    cluster: {
        scheduler: 'mcu-cluster-scheduler',
        registry: 'mcu-cluster-registry'
    },
    monitor: {
        sentinel: "sentinel",
        monitor: "monitor"
    },
    redis: {
        host: "redis",
        port: 6379,
        password: "privated"
    },
    rabbit: [
        {
            auth:"yulu:yulu",
            url:"rabbitmq:5672",
            monitorUrl: "http://rabbitmq:15672/api/health/checks/port-listener/5672",
        }
    ],
        rabbits: {
                "test-pro": {
                        limit: -1,
                        priority: 0,
                        enable: true,
                        urls: [
        {
            auth:"yulu:yulu",
            url:"rabbitmq:5672",
            monitorUrl: "http://rabbitmq:15672/api/health/checks/port-listener/5672",
        }
                        ]
                }
        },

    schedule: {
        "mcu": 0.25,
        "mock-mcu": 0.004,
        "asr": 0.05,
        "pageRecord": 0.1
    }
};
exports.configs = configs;
