"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.configs = void 0;
const os = require("os");
const configs = {
    logLevel: "debug",
    kafkaBrokers: [],
    kafkaTopicBrtc: '',
    kafkaTopicBmcu: '',
    recordPathPrefix: "/record",
    sasssRecordPathPrefix: "/bjy",

    trtcSignalServer: "wss://signaling.rtc.qq.com",
    brtcVloudBrtcAppId: '',
    retryDelayPolicy: [100, 500, 1000],
    numWorkers: 1,
    workerSettings: {
        log2debug: true,
        enableWebrtcLog: false,
        waterMarkFont: process.env.MCU_FONT || `fonts/font.ttf`,
        coverPath: process.env.MCU_COVER_PATH || `fonts/640x360.png`,
      
    },
    taskIdleTime : 1000 * 60 * 60 * 3, // 3 hours.
    postProcessQueue: "cloudproc",
    monitorNotify: "monitor",
    cluster: {
        purpose: "mcu",
        scheduler: 'mcu-cluster-scheduler',
        registry: 'mcu-cluster-registry',
        join_retry: 60,
        ip:process.env.POD_IP,
        port: '12009',
        load: {
            period: 1000,
            item: {
                name: 'cpu'
            }
        },
   },

   rabbit: [
		{
			auth:"yulu:yulu",
			url:"127.0.0.1:5672",
			monitorUrl: "http://127.0.0.1:15672/api/health/checks/port-listener/5672",
		}
   ],
   redis: {
           host: "127.0.0.1",
           port: 6379,
           password: "privated"
   },
    mock: {
            use: false,
            count: 25,
            responseDelay: 0, //ms
    }
}
exports.configs = configs;
