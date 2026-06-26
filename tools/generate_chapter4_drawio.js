const fs = require("fs");
const path = require("path");

const OUTPUT_PATH = path.join(__dirname, "..", "docs", "chapter4_flowcharts.drawio");
let globalId = 1;

const nextId = (prefix) => `${prefix}-${globalId++}`;

const escapeXml = (value) =>
  String(value)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#39;");

const htmlValue = (text) => {
  const lines = String(text).split("\n");
  const html = lines
    .map((line, index) => (index === 0 ? line : `<div>${line}</div>`))
    .join("");
  return escapeXml(html);
};

const STYLES = {
  title:
    "rounded=1;whiteSpace=wrap;html=1;fillColor=#FFFFFF;strokeColor=#000000;fontColor=#000000;fontSize=24;fontStyle=1;align=center;verticalAlign=middle;spacing=12;strokeWidth=2;",
  box:
    "rounded=1;whiteSpace=wrap;html=1;fillColor=#FFFFFF;strokeColor=#000000;fontColor=#000000;fontSize=20;align=center;verticalAlign=middle;spacing=10;strokeWidth=2;",
  boxStrong:
    "rounded=1;whiteSpace=wrap;html=1;fillColor=#FFFFFF;strokeColor=#000000;fontColor=#000000;fontSize=22;fontStyle=1;align=center;verticalAlign=middle;spacing=10;strokeWidth=2;",
  decision:
    "rhombus;whiteSpace=wrap;html=1;fillColor=#FFFFFF;strokeColor=#000000;fontColor=#000000;fontSize=20;fontStyle=1;align=center;verticalAlign=middle;spacing=10;strokeWidth=2;",
  terminator:
    "ellipse;whiteSpace=wrap;html=1;fillColor=#FFFFFF;strokeColor=#000000;fontColor=#000000;fontSize=22;fontStyle=1;align=center;verticalAlign=middle;spacing=10;strokeWidth=2;",
  note:
    "rounded=1;whiteSpace=wrap;html=1;fillColor=#FFFFFF;strokeColor=#000000;fontColor=#000000;fontSize=18;align=center;verticalAlign=middle;spacing=10;strokeWidth=2;dashed=1;",
  edge:
    "edgeStyle=orthogonalEdgeStyle;rounded=0;orthogonalLoop=1;jettySize=auto;html=1;strokeColor=#000000;strokeWidth=2;endArrow=block;endFill=1;",
  edgeLabel:
    "edgeStyle=orthogonalEdgeStyle;rounded=0;orthogonalLoop=1;jettySize=auto;html=1;strokeColor=#000000;strokeWidth=2;endArrow=block;endFill=1;fontSize=18;labelBackgroundColor=#FFFFFF;",
};

function createPage(name, build, options = {}) {
  const pageWidth = options.pageWidth || 1600;
  const pageHeight = options.pageHeight || 980;
  const yOffset = options.yOffset ?? -100;
  const cells = [];

  const addVertex = ({ value, x, y, w, h, style = STYLES.box }) => {
    if (style === STYLES.title) {
      return nextId("title");
    }
    const id = nextId("v");
    cells.push(
      `        <mxCell id="${id}" value="${htmlValue(value)}" style="${style}" vertex="1" parent="1">
          <mxGeometry x="${x}" y="${y + yOffset}" width="${w}" height="${h}" as="geometry"/>
        </mxCell>`
    );
    return id;
  };

  const addEdge = ({
    source,
    target,
    label = "",
    style = "",
    points = [],
  }) => {
    if (
      (source && String(source).startsWith("title-")) ||
      (target && String(target).startsWith("title-"))
    ) {
      return null;
    }
    const id = nextId("e");
    const mergedStyle = `${label ? STYLES.edgeLabel : STYLES.edge}${style}`;
    const pointsXml =
      points.length > 0
        ? `
            <Array as="points">
${points
  .map((point) => `              <mxPoint x="${point.x}" y="${point.y + yOffset}"/>`)
  .join("\n")}
            </Array>`
        : "";
    const geometry =
      points.length > 0
        ? `          <mxGeometry relative="1" as="geometry">${pointsXml}
          </mxGeometry>`
        : `          <mxGeometry relative="1" as="geometry"/>`;
    cells.push(
      `        <mxCell id="${id}" value="${escapeXml(label)}" style="${mergedStyle}" edge="1" parent="1" source="${source}" target="${target}">
${geometry}
        </mxCell>`
    );
    return id;
  };

  build({ addVertex, addEdge });

  return `  <diagram name="${escapeXml(name)}" id="${nextId("page")}">
    <mxGraphModel dx="${pageWidth}" dy="${pageHeight}" grid="1" gridSize="10" guides="1" tooltips="1" connect="1" arrows="1" fold="1" page="1" pageScale="1" pageWidth="${pageWidth}" pageHeight="${pageHeight}" math="0" shadow="0">
      <root>
        <mxCell id="0"/>
        <mxCell id="1" parent="0"/>
${cells.join("\n")}
      </root>
    </mxGraphModel>
  </diagram>`;
}

const pages = [];

pages.push(
  createPage("4.1 软件总体设计", ({ addVertex, addEdge }) => {
    const title = addVertex({
      value: "4.1 软件总体设计\nESP-IDF + FreeRTOS 多任务协同",
      x: 430,
      y: 20,
      w: 740,
      h: 72,
      style: STYLES.title,
    });
    const appMain = addVertex({
      value: "app_main()\n系统启动入口",
      x: 590,
      y: 126,
      w: 420,
      h: 86,
      style: STYLES.boxStrong,
    });
    const controlDomain = addVertex({
      value: "系统初始化与联网控制\n主控初始化 -> WiFi 配网 / 网络切换",
      x: 90,
      y: 286,
      w: 360,
      h: 116,
      style: STYLES.boxStrong,
    });
    const dataDomain = addVertex({
      value: "传感采集与数据上云\n空气与土壤采集 -> MQTT / 云平台通信",
      x: 620,
      y: 286,
      w: 360,
      h: 116,
      style: STYLES.boxStrong,
    });
    const maintainDomain = addVertex({
      value: "节点通信与维护升级\nLoRa 通信 / 子节点管理 -> 本地存储 / OTA 升级",
      x: 1150,
      y: 286,
      w: 360,
      h: 116,
      style: STYLES.boxStrong,
    });
    const coordination = addVertex({
      value: "统一状态标志 SysEventFlag\n模块通过回调、任务和接口函数协同",
      x: 370,
      y: 484,
      w: 860,
      h: 98,
      style: STYLES.boxStrong,
    });
    const tasks = addVertex({
      value:
        "关键后台任务\nsensor_upload / lora_poll / cat1_delay / ota_bootstrap",
      x: 430,
      y: 634,
      w: 740,
      h: 88,
    });
    const finalBox = addVertex({
      value: "多任务并发运行\n采集、联网、节点管理与升级维护同步推进",
      x: 500,
      y: 770,
      w: 600,
      h: 92,
      style: STYLES.boxStrong,
    });

    addEdge({ source: title, target: appMain, style: "exitY=1;entryY=0;" });
    addEdge({
      source: appMain,
      target: controlDomain,
      style: "exitX=0.2;exitY=1;entryX=0.5;entryY=0;",
    });
    addEdge({
      source: appMain,
      target: dataDomain,
      style: "exitX=0.5;exitY=1;entryX=0.5;entryY=0;",
    });
    addEdge({
      source: appMain,
      target: maintainDomain,
      style: "exitX=0.8;exitY=1;entryX=0.5;entryY=0;",
    });
    addEdge({
      source: controlDomain,
      target: coordination,
      style: "exitX=0.5;exitY=1;entryX=0.2;entryY=0;",
    });
    addEdge({
      source: dataDomain,
      target: coordination,
      style: "exitX=0.5;exitY=1;entryX=0.5;entryY=0;",
    });
    addEdge({
      source: maintainDomain,
      target: coordination,
      style: "exitX=0.5;exitY=1;entryX=0.8;entryY=0;",
    });
    addEdge({ source: coordination, target: tasks, style: "exitY=1;entryY=0;" });
    addEdge({ source: tasks, target: finalBox, style: "exitY=1;entryY=0;" });
  }, { pageHeight: 920 })
);

pages.push(
  createPage("4.2 主程序模块设计", ({ addVertex, addEdge }) => {
    const title = addVertex({
      value: "4.2 主程序模块设计",
      x: 520,
      y: 20,
      w: 560,
      h: 70,
      style: STYLES.title,
    });
    const start = addVertex({
      value: "app_main()",
      x: 680,
      y: 120,
      w: 240,
      h: 62,
      style: STYLES.terminator,
    });
    const nvs = addVertex({
      value: "EEprom_Init() + EEprom_ReadInfo()",
      x: 530,
      y: 220,
      w: 540,
      h: 74,
    });
    const gpio = addVertex({
      value: "配置土壤传感器供电引脚\nWiFi_Cat1_InitGPIO() 并默认关闭 Cat1",
      x: 530,
      y: 328,
      w: 540,
      h: 88,
    });
    const wifiInit = addVertex({
      value: "bsp_led_init() + wifi_manager_init()\n注册 wifi_state_callback",
      x: 530,
      y: 450,
      w: 540,
      h: 88,
    });
    const hasSaved = addVertex({
      value: "存在已保存 WiFi 配置？",
      x: 640,
      y: 576,
      w: 320,
      h: 130,
      style: STYLES.decision,
    });
    const saved = addVertex({
      value: "wifi_manager_connect()\n连接历史 SSID / Password",
      x: 110,
      y: 612,
      w: 320,
      h: 86,
    });
    const ap = addVertex({
      value: "wifi_manager_start_ap_provisioning()\n启动 ESP32_Config 配网热点",
      x: 1170,
      y: 612,
      w: 320,
      h: 86,
    });
    const iic = addVertex({
      value: "iic_sensor_task_start()\n启动空气传感器采集任务",
      x: 530,
      y: 746,
      w: 540,
      h: 86,
    });
    const tasks = addVertex({
      value: "创建 sensor_upload 与 cat1_delay 任务",
      x: 530,
      y: 862,
      w: 540,
      h: 74,
    });

    addEdge({ source: title, target: start, style: "exitY=1;entryY=0;" });
    addEdge({ source: start, target: nvs, style: "exitY=1;entryY=0;" });
    addEdge({ source: nvs, target: gpio, style: "exitY=1;entryY=0;" });
    addEdge({ source: gpio, target: wifiInit, style: "exitY=1;entryY=0;" });
    addEdge({ source: wifiInit, target: hasSaved, style: "exitY=1;entryY=0;" });
    addEdge({
      source: hasSaved,
      target: saved,
      label: "是",
      style: "exitX=0;exitY=0.5;entryX=1;entryY=0.5;",
    });
    addEdge({
      source: hasSaved,
      target: ap,
      label: "否",
      style: "exitX=1;exitY=0.5;entryX=0;entryY=0.5;",
    });
    addEdge({
      source: saved,
      target: iic,
      style: "exitX=0.5;exitY=1;entryX=0.25;entryY=0;",
    });
    addEdge({
      source: ap,
      target: iic,
      style: "exitX=0.5;exitY=1;entryX=0.75;entryY=0;",
    });
    addEdge({ source: iic, target: tasks, style: "exitY=1;entryY=0;" });

    const lora = addVertex({
      value: "soil_sensor_init() + LoRa_Init()",
      x: 530,
      y: 970,
      w: 540,
      h: 74,
    });
    const loraOk = addVertex({
      value: "LoRa 初始化成功？",
      x: 645,
      y: 1084,
      w: 310,
      h: 122,
      style: STYLES.decision,
    });
    const loraTask = addVertex({
      value: "创建 lora_poll_task",
      x: 220,
      y: 1110,
      w: 290,
      h: 74,
    });
    const loraFail = addVertex({
      value: "记录错误并跳过 LoRa 轮询任务",
      x: 1090,
      y: 1110,
      w: 320,
      h: 74,
    });
    const loop = addVertex({
      value: "主循环每 10s 保持运行",
      x: 575,
      y: 1260,
      w: 450,
      h: 74,
      style: STYLES.boxStrong,
    });

    addEdge({ source: tasks, target: lora, style: "exitY=1;entryY=0;" });
    addEdge({ source: lora, target: loraOk, style: "exitY=1;entryY=0;" });
    addEdge({
      source: loraOk,
      target: loraTask,
      label: "是",
      style: "exitX=0;exitY=0.5;entryX=1;entryY=0.5;",
    });
    addEdge({
      source: loraOk,
      target: loraFail,
      label: "否",
      style: "exitX=1;exitY=0.5;entryX=0;entryY=0.5;",
    });
    addEdge({
      source: loraTask,
      target: loop,
      style: "exitY=1;entryX=0.3;entryY=0;",
    });
    addEdge({
      source: loraFail,
      target: loop,
      style: "exitY=1;entryX=0.7;entryY=0;",
    });
  }, { pageHeight: 1380 })
);

pages.push(
  createPage("4.3 WiFi 配网与网络切换设计", ({ addVertex, addEdge }) => {
    const HIDDEN_POINT =
      "ellipse;whiteSpace=wrap;html=1;fillColor=none;strokeColor=none;opacity=0;resizable=0;movable=0;deletable=0;rotatable=0;";
    const title = addVertex({
      value: "4.3 WiFi 配网与网络切换设计",
      x: 420,
      y: 20,
      w: 760,
      h: 70,
      style: STYLES.title,
    });
    const start = addVertex({
      value: "设备启动\nwifi_manager_init() 完成",
      x: 625,
      y: 120,
      w: 350,
      h: 76,
      style: STYLES.terminator,
    });
    const hasSaved = addVertex({
      value: "是否存在历史 WiFi 配置？",
      x: 650,
      y: 230,
      w: 300,
      h: 120,
      style: STYLES.decision,
    });
    const saved = addVertex({
      value: "直接使用已保存的\nSSID / Password",
      x: 100,
      y: 244,
      w: 390,
      h: 90,
    });
    const apFlow = addVertex({
      value: "进入 AP 配网模式\n启动 AP / DHCP / DNS / HTTP 配网页面",
      x: 1060,
      y: 232,
      w: 420,
      h: 100,
    });
    const submit = addVertex({
      value: "手机 / 电脑连接 ESP32_Config\n提交 SSID 和 Password",
      x: 1060,
      y: 370,
      w: 420,
      h: 88,
    });
    const connect = addVertex({
      value: "wifi_manager_connect()\n发起 STA 连接",
      x: 590,
      y: 404,
      w: 420,
      h: 86,
    });
    const gotIp = addVertex({
      value: "是否成功获取 IP？\n(IP_EVENT_STA_GOT_IP)",
      x: 660,
      y: 534,
      w: 280,
      h: 118,
      style: STYLES.decision,
    });
    const gotIpTop = addVertex({
      value: "",
      x: 799,
      y: 506,
      w: 2,
      h: 2,
      style: HIDDEN_POINT,
    });
    const success = addVertex({
      value: "保存 wifi_sta_cfg\n关闭 AP / DNS / HTTP\n切回纯 STA 模式并关闭 Cat1",
      x: 120,
      y: 694,
      w: 420,
      h: 118,
    });
    const callback = addVertex({
      value: "wifi_state_callback()\n启动 MQTT 与 OTA bootstrap",
      x: 120,
      y: 852,
      w: 420,
      h: 84,
      style: STYLES.boxStrong,
    });
    const retry = addVertex({
      value: "连接断开后自动重试\nWIFI_EVENT_STA_DISCONNECTED / esp_wifi_connect() 最多 6 次",
      x: 1010,
      y: 688,
      w: 470,
      h: 100,
    });
    const fallback = addVertex({
      value: "仍未连接成功\n回到 AP 配网入口重新提交",
      x: 1080,
      y: 834,
      w: 330,
      h: 84,
    });
    const cat1Note = addVertex({
      value: "补充：cat1_delayed_start_task 在 120s 后检查 WiFi\n若仍不可用，则启动 Cat1 备份链路",
      x: 430,
      y: 978,
      w: 740,
      h: 84,
      style: STYLES.note,
    });

    addEdge({ source: title, target: start, style: "exitY=1;entryY=0;" });
    addEdge({ source: start, target: hasSaved, style: "exitY=1;entryY=0;" });
    addEdge({
      source: hasSaved,
      target: saved,
      label: "是",
      style: "exitX=0;exitY=0.5;entryX=1;entryY=0.5;",
    });
    addEdge({
      source: hasSaved,
      target: apFlow,
      label: "否",
      style: "exitX=1;exitY=0.5;entryX=0;entryY=0.5;",
    });
    addEdge({ source: apFlow, target: submit, style: "exitY=1;entryY=0;" });
    addEdge({
      source: saved,
      target: connect,
      style: "exitY=1;entryX=0;entryY=0.5;",
    });
    addEdge({
      source: submit,
      target: connect,
      style: "exitX=0;exitY=0.5;entryX=1;entryY=0.5;",
    });
    addEdge({
      source: connect,
      target: gotIpTop,
      style: "exitX=0.5;exitY=1;entryX=0.5;entryY=0;endArrow=none;",
    });
    addEdge({
      source: gotIpTop,
      target: gotIp,
      style: "exitX=0.5;exitY=1;entryX=0.5;entryY=0;",
    });
    addEdge({
      source: gotIp,
      target: success,
      label: "是",
      style: "exitX=0;exitY=0.5;entryX=1;entryY=0.5;",
    });
    addEdge({ source: success, target: callback, style: "exitY=1;entryY=0;" });
    addEdge({
      source: gotIp,
      target: retry,
      label: "否",
      style: "exitX=1;exitY=0.5;entryX=0;entryY=0.5;",
    });
    addEdge({ source: retry, target: fallback, style: "exitY=1;entryY=0;" });
    addEdge({
      source: fallback,
      target: apFlow,
      style: "exitX=1;exitY=0.5;entryX=1;entryY=0.5;",
      points: [{ x: 1540, y: 876 }, { x: 1540, y: 282 }],
    });
    addEdge({
      source: callback,
      target: cat1Note,
      style: "dashed=1;endArrow=none;strokeColor=#000000;",
    });
  }, { pageHeight: 1080 })
);

pages.push(
  createPage("4.4 LoRa 通信与子节点管理设计", ({ addVertex, addEdge }) => {
    const title = addVertex({
      value: "4.4 LoRa 通信与子节点管理设计",
      x: 450,
      y: 20,
      w: 700,
      h: 70,
      style: STYLES.title,
    });
    const init = addVertex({
      value: "LoRa_Init()\n115200 / 9600 探测 + AT+DEFAULT + 透明传输模式",
      x: 470,
      y: 126,
      w: 660,
      h: 92,
      style: STYLES.boxStrong,
    });
    const query = addVertex({
      value: "LoRa_QueryNodeOnline()\n定时发送在线查询帧",
      x: 535,
      y: 262,
      w: 530,
      h: 86,
    });
    const active = addVertex({
      value: "LoRa_ActiveEvent()\n读取 UART 原始数据并写入 staging 缓冲",
      x: 490,
      y: 390,
      w: 620,
      h: 88,
    });
    const parse = addVertex({
      value: "查找帧头 AA55 + 检查长度",
      x: 560,
      y: 516,
      w: 480,
      h: 74,
    });
    const crc = addVertex({
      value: "CRC8-Maxim 校验",
      x: 610,
      y: 624,
      w: 380,
      h: 74,
    });
    const cmd = addVertex({
      value: "命令类型？",
      x: 665,
      y: 740,
      w: 270,
      h: 118,
      style: STYLES.decision,
    });
    const status = addVertex({
      value: "CMD_QUERY\n确认子节点在线\n置位 SUB_LORA_CONFIRMED",
      x: 90,
      y: 930,
      w: 330,
      h: 116,
    });
    const report = addVertex({
      value: "CMD_REPORT\n解析光照 / 温度 / 湿度\n缓存 last_node_data 并置位 SUB_NODE_DATA_READY",
      x: 560,
      y: 914,
      w: 480,
      h: 132,
      style: STYLES.boxStrong,
    });
    const control = addVertex({
      value: "CMD_CONTROL\n接收节点 LED 控制响应",
      x: 1160,
      y: 930,
      w: 340,
      h: 100,
    });
    const cloudCtrl = addVertex({
      value: "平台下发 PowerSwitch_2\nLoRa_ControlNodeLED() 转发到子节点",
      x: 1160,
      y: 760,
      w: 340,
      h: 90,
      style: STYLES.note,
    });
    const online = addVertex({
      value: "若 MQTT 已连接\n调用 WiFi_Cat1_SubOnline() 上报子设备上线",
      x: 70,
      y: 1096,
      w: 370,
      h: 86,
      style: STYLES.note,
    });
    const timeout = addVertex({
      value: "25s 内没有有效协议帧\n清除 SUB_LORA_CONFIRMED / SUB_NODE_DATA_READY",
      x: 465,
      y: 1110,
      w: 670,
      h: 84,
      style: STYLES.note,
    });

    addEdge({ source: title, target: init, style: "exitY=1;entryY=0;" });
    addEdge({ source: init, target: query, style: "exitY=1;entryY=0;" });
    addEdge({ source: query, target: active, style: "exitY=1;entryY=0;" });
    addEdge({ source: active, target: parse, style: "exitY=1;entryY=0;" });
    addEdge({ source: parse, target: crc, style: "exitY=1;entryY=0;" });
    addEdge({ source: crc, target: cmd, style: "exitY=1;entryY=0;" });
    addEdge({
      source: cmd,
      target: status,
      label: "QUERY",
      style: "exitX=0.15;exitY=1;entryX=0.5;entryY=0;",
      points: [{ x: 706, y: 890 }, { x: 255, y: 890 }],
    });
    addEdge({
      source: cmd,
      target: report,
      label: "REPORT",
      style: "exitX=0.5;exitY=1;entryX=0.5;entryY=0;",
    });
    addEdge({
      source: cmd,
      target: control,
      label: "CONTROL",
      style: "exitX=0.85;exitY=1;entryX=0.5;entryY=0;",
      points: [{ x: 895, y: 890 }, { x: 1330, y: 890 }],
    });
    addEdge({ source: status, target: online, style: "exitY=1;entryY=0;" });
    addEdge({
      source: cloudCtrl,
      target: control,
      style: "exitX=0.5;exitY=1;entryX=0.5;entryY=0;dashed=1;strokeColor=#000000;",
    });
    addEdge({
      source: report,
      target: timeout,
      style: "exitX=0.5;exitY=1;entryX=0.5;entryY=0;dashed=1;endArrow=none;strokeColor=#000000;",
    });
  }, { pageHeight: 1220 })
);

pages.push(
  createPage("4.5 Cat1 联网与 MQTT 通信设计", ({ addVertex, addEdge }) => {
    const HIDDEN_POINT =
      "ellipse;whiteSpace=wrap;html=1;fillColor=none;strokeColor=none;opacity=0;resizable=0;movable=0;deletable=0;rotatable=0;";
    const title = addVertex({
      value: "4.5 Cat1 联网与 MQTT 通信设计",
      x: 430,
      y: 20,
      w: 740,
      h: 70,
      style: STYLES.title,
    });
    const netChoice = addVertex({
      value: "WiFi 链路是否可用？",
      x: 650,
      y: 126,
      w: 300,
      h: 114,
      style: STYLES.decision,
    });
    const wifiPath = addVertex({
      value: "WiFi 已连接\nesp_mqtt_app_start()",
      x: 140,
      y: 286,
      w: 340,
      h: 84,
    });
    const cat1Path = addVertex({
      value: "120s 内仍无 WiFi\n启动 Cat1_AT_Mqtt_Task",
      x: 1120,
      y: 286,
      w: 340,
      h: 84,
    });
    const token = addVertex({
      value: "MQTT_Init()\n计算 OneNET token 与 Topic",
      x: 140,
      y: 414,
      w: 340,
      h: 84,
    });
    const cat1Dial = addVertex({
      value: "AT 检测 / 注网 / APN / QIACT\nQMTOPEN + QMTCONN 建立会话",
      x: 1080,
      y: 402,
      w: 420,
      h: 106,
    });
    const connected = addVertex({
      value: "建立 MQTT 会话\n置位 CONNECT_MQTT 并订阅 9 个 Topic",
      x: 525,
      y: 566,
      w: 550,
      h: 84,
      style: STYLES.boxStrong,
    });
    const subOnline = addVertex({
      value: "若 LoRa 已确认\n调用 WiFi_Cat1_SubOnline() 上报子设备上线",
      x: 525,
      y: 692,
      w: 550,
      h: 84,
    });
    const upload = addVertex({
      value: "统一数据上报\nGatewayDataPost / SoilDataPost / NodeDataPost",
      x: 100,
      y: 834,
      w: 390,
      h: 96,
    });
    const downlink = addVertex({
      value: "下行属性控制\nPestAlarm -> 板载 LED\nPowerSwitch_2 -> LoRa_ControlNodeLED",
      x: 575,
      y: 828,
      w: 450,
      h: 110,
    });
    const ota = addVertex({
      value: "收到 /ota/inform\n回发 inform_reply -> OneNET_FuseOTA_CheckTask",
      x: 1110,
      y: 834,
      w: 390,
      h: 96,
    });
    const cloud = addVertex({
      value: "OneNET 云平台\n数据上报 + 指令下发 + OTA 通知",
      x: 500,
      y: 1006,
      w: 600,
      h: 86,
      style: STYLES.boxStrong,
    });
    const tokenDrop = addVertex({
      value: "",
      x: 309,
      y: 532,
      w: 2,
      h: 2,
      style: HIDDEN_POINT,
    });
    const mergeCenter = addVertex({
      value: "",
      x: 799,
      y: 532,
      w: 2,
      h: 2,
      style: HIDDEN_POINT,
    });
    const cat1Drop = addVertex({
      value: "",
      x: 1289,
      y: 532,
      w: 2,
      h: 2,
      style: HIDDEN_POINT,
    });

    addEdge({ source: title, target: netChoice, style: "exitY=1;entryY=0;" });
    addEdge({
      source: netChoice,
      target: wifiPath,
      label: "是",
      style: "exitX=0;exitY=0.5;entryX=1;entryY=0.5;",
    });
    addEdge({
      source: netChoice,
      target: cat1Path,
      label: "否",
      style: "exitX=1;exitY=0.5;entryX=0;entryY=0.5;",
    });
    addEdge({ source: wifiPath, target: token, style: "exitY=1;entryY=0;" });
    addEdge({
      source: token,
      target: tokenDrop,
      style: "exitX=0.5;exitY=1;entryX=0.5;entryY=0;endArrow=none;",
    });
    addEdge({
      source: tokenDrop,
      target: mergeCenter,
      style: "exitX=1;exitY=0.5;entryX=0;entryY=0.5;endArrow=none;",
    });
    addEdge({ source: cat1Path, target: cat1Dial, style: "exitY=1;entryY=0;" });
    addEdge({
      source: cat1Dial,
      target: cat1Drop,
      style: "exitX=0.5;exitY=1;entryX=0.5;entryY=0;endArrow=none;",
    });
    addEdge({
      source: cat1Drop,
      target: mergeCenter,
      style: "exitX=0;exitY=0.5;entryX=1;entryY=0.5;endArrow=none;",
    });
    addEdge({
      source: mergeCenter,
      target: connected,
      style: "exitX=0.5;exitY=1;entryX=0.5;entryY=0;",
    });
    addEdge({ source: connected, target: subOnline, style: "exitY=1;entryY=0;" });
    addEdge({ source: subOnline, target: upload, style: "exitX=0;exitY=1;entryX=0.5;entryY=0;" });
    addEdge({ source: subOnline, target: downlink, style: "exitY=1;entryY=0;" });
    addEdge({ source: subOnline, target: ota, style: "exitX=1;exitY=1;entryX=0.5;entryY=0;" });
    addEdge({ source: upload, target: cloud, style: "exitY=1;entryX=0.2;entryY=0;" });
    addEdge({ source: downlink, target: cloud, style: "exitY=1;entryX=0.5;entryY=0;" });
    addEdge({ source: ota, target: cloud, style: "exitY=1;entryX=0.8;entryY=0;" });
  }, { pageHeight: 1120 })
);

pages.push(
  createPage("4.6 本地存储设计", ({ addVertex, addEdge }) => {
    const title = addVertex({
      value: "4.6 本地存储设计\nESP32 NVS 持久化支撑",
      x: 470,
      y: 20,
      w: 660,
      h: 72,
      style: STYLES.title,
    });
    const init = addVertex({
      value: "EEprom_Init()\nnvs_flash_init()",
      x: 615,
      y: 140,
      w: 370,
      h: 84,
      style: STYLES.boxStrong,
    });
    const kv = addVertex({
      value: "EEprom_ReadData / EEprom_WriteData\nnvs_open + get_blob / set_blob + nvs_commit",
      x: 400,
      y: 282,
      w: 800,
      h: 94,
      style: STYLES.boxStrong,
    });
    const info = addVertex({
      value: "info_data\n网关 / 子设备版本\nOTA 标志与长度信息",
      x: 110,
      y: 446,
      w: 320,
      h: 110,
    });
    const wifi = addVertex({
      value: "wifi_sta_cfg\nSSID / Password\n仅在 GOT_IP 后写入",
      x: 640,
      y: 446,
      w: 320,
      h: 110,
    });
    const pending = addVertex({
      value: "ota_pending\nstatus_url / target_version\n用于升级后成功确认",
      x: 1170,
      y: 446,
      w: 320,
      h: 110,
    });
    const boot = addVertex({
      value: "开机读取版本与历史配置\n决定直接连网还是进入 AP 配网",
      x: 110,
      y: 636,
      w: 320,
      h: 96,
    });
    const gotIp = addVertex({
      value: "WiFi 拿到 IP 后\n保存成功凭据，避免错误密码覆盖旧配置",
      x: 640,
      y: 636,
      w: 320,
      h: 96,
    });
    const otaConfirm = addVertex({
      value: "新固件启动后\n读取 ota_pending 并上报 OTA step=201",
      x: 1170,
      y: 636,
      w: 320,
      h: 96,
    });
    const summary = addVertex({
      value: "目的\n保证重启后仍能恢复联网状态、版本信息与升级确认流程",
      x: 390,
      y: 820,
      w: 820,
      h: 92,
      style: STYLES.note,
    });

    addEdge({ source: title, target: init, style: "exitY=1;entryY=0;" });
    addEdge({ source: init, target: kv, style: "exitY=1;entryY=0;" });
    addEdge({
      source: kv,
      target: info,
      style: "exitX=0.15;exitY=1;entryY=0;",
    });
    addEdge({ source: kv, target: wifi, style: "exitY=1;entryY=0;" });
    addEdge({
      source: kv,
      target: pending,
      style: "exitX=0.85;exitY=1;entryY=0;",
    });
    addEdge({ source: info, target: boot, style: "exitY=1;entryY=0;" });
    addEdge({ source: wifi, target: gotIp, style: "exitY=1;entryY=0;" });
    addEdge({ source: pending, target: otaConfirm, style: "exitY=1;entryY=0;" });
    addEdge({ source: boot, target: summary, style: "dashed=1;strokeColor=#000000;" });
    addEdge({ source: gotIp, target: summary, style: "dashed=1;strokeColor=#000000;" });
    addEdge({ source: otaConfirm, target: summary, style: "dashed=1;strokeColor=#000000;" });
  }, { pageHeight: 980 })
);

pages.push(
  createPage("4.7 传感器数据采集与处理设计", ({ addVertex, addEdge }) => {
    const title = addVertex({
      value: "4.7 传感器数据采集与处理设计",
      x: 430,
      y: 20,
      w: 740,
      h: 70,
      style: STYLES.title,
    });
    const entry = addVertex({
      value: "多源数据采集入口\nI2C / UART / LoRa 数据并行到达",
      x: 560,
      y: 118,
      w: 480,
      h: 76,
      style: STYLES.boxStrong,
    });
    const air = addVertex({
      value: "SHT30 / BH1750\nI2C 采集温湿度 / 光照",
      x: 80,
      y: 244,
      w: 360,
      h: 90,
      style: STYLES.boxStrong,
    });
    const soil = addVertex({
      value: "土壤传感器\nUART Modbus 查询 N / P / K 等参数",
      x: 620,
      y: 244,
      w: 360,
      h: 90,
      style: STYLES.boxStrong,
    });
    const node = addVertex({
      value: "LoRa 子节点上报\n温度 / 湿度 / 光照",
      x: 1160,
      y: 244,
      w: 360,
      h: 90,
      style: STYLES.boxStrong,
    });
    const airTask = addVertex({
      value: "iic_sensor_task 周期读取\n更新 g_sensor_data",
      x: 95,
      y: 392,
      w: 330,
      h: 84,
    });
    const soilTask = addVertex({
      value: "soil_sensor_read_data()\n发送查询帧 -> 读取 21 字节 -> CRC16 校验",
      x: 585,
      y: 380,
      w: 430,
      h: 108,
    });
    const nodeTask = addVertex({
      value: "LoRa 帧解析成功后\n更新 last_node_data 缓存",
      x: 1180,
      y: 392,
      w: 320,
      h: 84,
    });
    const aggregate = addVertex({
      value:
        "数据归一化与缓存整合\n空气数据更新 g_sensor_data；土壤数据换算为业务值；LoRa 数据更新 last_node_data",
      x: 525,
      y: 554,
      w: 550,
      h: 96,
    });
    const uploadTask = addVertex({
      value: "统一上传调度任务\nunified_sensor_upload_task",
      x: 560,
      y: 694,
      w: 480,
      h: 82,
      style: STYLES.boxStrong,
    });
    const mqttReady = addVertex({
      value: "当前是否允许上传？\n(MQTT 已连接且未处于 OTA 中)",
      x: 600,
      y: 820,
      w: 400,
      h: 122,
      style: STYLES.decision,
    });
    const upload = addVertex({
      value: "WiFi_Cat1_GatewayDataPost()\nWiFi_Cat1_SoilDataPost()\nWiFi_Cat1_NodeDataPost()",
      x: 140,
      y: 992,
      w: 420,
      h: 116,
    });
    const wait = addVertex({
      value: "暂不上传，进入下一轮判断",
      x: 1050,
      y: 1008,
      w: 360,
      h: 84,
    });
    const note = addVertex({
      value: "上传周期约 30s；OTA 下载期间暂停上传，避免通信与写入互相干扰",
      x: 400,
      y: 1134,
      w: 800,
      h: 84,
      style: STYLES.note,
    });

    addEdge({ source: title, target: entry, style: "exitY=1;entryY=0;" });
    addEdge({
      source: entry,
      target: air,
      style: "exitX=0.2;exitY=1;entryX=0.5;entryY=0;",
    });
    addEdge({ source: entry, target: soil, style: "exitY=1;entryY=0;" });
    addEdge({
      source: entry,
      target: node,
      style: "exitX=0.8;exitY=1;entryX=0.5;entryY=0;",
    });
    addEdge({ source: air, target: airTask, style: "exitY=1;entryY=0;" });
    addEdge({ source: soil, target: soilTask, style: "exitY=1;entryY=0;" });
    addEdge({ source: node, target: nodeTask, style: "exitY=1;entryY=0;" });
    addEdge({
      source: airTask,
      target: aggregate,
      style: "exitX=1;exitY=0.5;entryX=0.1;entryY=0.5;",
    });
    addEdge({ source: soilTask, target: aggregate, style: "exitY=1;entryY=0;" });
    addEdge({
      source: nodeTask,
      target: aggregate,
      style: "exitX=0;exitY=0.5;entryX=0.9;entryY=0.5;",
    });
    addEdge({ source: aggregate, target: uploadTask, style: "exitY=1;entryY=0;" });
    addEdge({ source: uploadTask, target: mqttReady, style: "exitY=1;entryY=0;" });
    addEdge({
      source: mqttReady,
      target: upload,
      label: "是",
      style: "exitX=0;exitY=0.5;entryX=1;entryY=0.5;",
    });
    addEdge({
      source: mqttReady,
      target: wait,
      label: "否",
      style: "exitX=1;exitY=0.5;entryX=0;entryY=0.5;",
    });
    addEdge({ source: upload, target: note, style: "dashed=1;strokeColor=#000000;" });
    addEdge({ source: wait, target: note, style: "dashed=1;strokeColor=#000000;" });
  }, { pageHeight: 1240 })
);

pages.push(
  createPage("4.8 OTA 升级设计", ({ addVertex, addEdge }) => {
    const title = addVertex({
      value: "4.8 OTA 升级设计",
      x: 520,
      y: 20,
      w: 560,
      h: 70,
      style: STYLES.title,
    });
    const start = addVertex({
      value: "启动后或收到 ota/inform",
      x: 610,
      y: 116,
      w: 380,
      h: 66,
      style: STYLES.terminator,
    });
    const check = addVertex({
      value: "OneNET_FuseOTA_CheckTask()\n查询 fuse-ota 升级任务",
      x: 530,
      y: 220,
      w: 540,
      h: 88,
    });
    const hasTask = addVertex({
      value: "存在可执行升级任务？",
      x: 635,
      y: 346,
      w: 330,
      h: 122,
      style: STYLES.decision,
    });
    const noTask = addVertex({
      value: "结束本次检查\n等待下一次触发",
      x: 1090,
      y: 370,
      w: 360,
      h: 84,
    });
    const saveMeta = addVertex({
      value: "保存升级任务元数据\nstatus_url / target_version / size / md5",
      x: 560,
      y: 510,
      w: 480,
      h: 84,
    });
    const startOta = addVertex({
      value: "WiFi_Cat1_StartOTA(url)\n置位 OTA_RUNNING",
      x: 560,
      y: 630,
      w: 480,
      h: 84,
    });
    const stream = addVertex({
      value: "perform_streaming_ota()\nHTTP GET 固件流",
      x: 560,
      y: 750,
      w: 480,
      h: 84,
    });
    const write = addVertex({
      value: "esp_ota_begin -> 分块读取 -> esp_ota_write",
      x: 560,
      y: 870,
      w: 480,
      h: 84,
    });
    const verify = addVertex({
      value: "长度校验 + MD5 校验",
      x: 560,
      y: 990,
      w: 480,
      h: 74,
    });
    const ok = addVertex({
      value: "固件写入与校验成功？",
      x: 645,
      y: 1100,
      w: 310,
      h: 122,
      style: STYLES.decision,
    });
    const fail = addVertex({
      value: "上报失败状态码\n清除 OTA_RUNNING / CONNECT_OTA",
      x: 1070,
      y: 1120,
      w: 360,
      h: 84,
    });
    const bootPart = addVertex({
      value: "esp_ota_end + esp_ota_set_boot_partition",
      x: 560,
      y: 1260,
      w: 480,
      h: 84,
    });
    const savePending = addVertex({
      value: "上报 step=100\n保存 ota_pending 并准备重启",
      x: 560,
      y: 1380,
      w: 480,
      h: 84,
    });
    const reboot = addVertex({
      value: "esp_restart()",
      x: 620,
      y: 1504,
      w: 360,
      h: 68,
      style: STYLES.terminator,
    });
    const bootOk = addVertex({
      value: "新固件启动后\n标记镜像有效",
      x: 620,
      y: 1610,
      w: 360,
      h: 84,
    });
    const report201 = addVertex({
      value: "上报 step=201\n清除 ota_pending",
      x: 620,
      y: 1730,
      w: 360,
      h: 84,
    });

    addEdge({ source: title, target: start, style: "exitY=1;entryY=0;" });
    addEdge({ source: start, target: check, style: "exitY=1;entryY=0;" });
    addEdge({ source: check, target: hasTask, style: "exitY=1;entryY=0;" });
    addEdge({
      source: hasTask,
      target: noTask,
      label: "否",
      style: "exitX=1;exitY=0.5;entryX=0;entryY=0.5;",
    });
    addEdge({
      source: hasTask,
      target: saveMeta,
      label: "是",
      style: "exitY=1;entryY=0;",
    });
    addEdge({ source: saveMeta, target: startOta, style: "exitY=1;entryY=0;" });
    addEdge({ source: startOta, target: stream, style: "exitY=1;entryY=0;" });
    addEdge({ source: stream, target: write, style: "exitY=1;entryY=0;" });
    addEdge({ source: write, target: verify, style: "exitY=1;entryY=0;" });
    addEdge({ source: verify, target: ok, style: "exitY=1;entryY=0;" });
    addEdge({
      source: ok,
      target: fail,
      label: "否",
      style: "exitX=1;exitY=0.5;entryX=0;entryY=0.5;",
    });
    addEdge({
      source: ok,
      target: bootPart,
      label: "是",
      style: "exitY=1;entryY=0;",
    });
    addEdge({ source: bootPart, target: savePending, style: "exitY=1;entryY=0;" });
    addEdge({ source: savePending, target: reboot, style: "exitY=1;entryY=0;" });
    addEdge({ source: reboot, target: bootOk, style: "exitY=1;entryY=0;" });
    addEdge({ source: bootOk, target: report201, style: "exitY=1;entryY=0;" });
  }, { pageHeight: 1860 })
);

const xml = `<mxfile host="app.diagrams.net" version="24.7.17">
${pages.join("\n")}
</mxfile>
`;

fs.mkdirSync(path.dirname(OUTPUT_PATH), { recursive: true });
fs.writeFileSync(OUTPUT_PATH, xml, "utf8");

console.log(`Generated ${OUTPUT_PATH}`);
