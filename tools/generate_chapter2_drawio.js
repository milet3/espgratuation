const fs = require("fs");
const path = require("path");

const OUTPUT_PATH = path.join(__dirname, "..", "docs", "chapter2_flowcharts.drawio");
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
  boxCompact:
    "rounded=1;whiteSpace=wrap;html=1;fillColor=#FFFFFF;strokeColor=#000000;fontColor=#000000;fontSize=18;align=center;verticalAlign=middle;spacing=8;strokeWidth=2;",
  boxStrong:
    "rounded=1;whiteSpace=wrap;html=1;fillColor=#FFFFFF;strokeColor=#000000;fontColor=#000000;fontSize=22;fontStyle=1;align=center;verticalAlign=middle;spacing=10;strokeWidth=2;",
  boxStrongCompact:
    "rounded=1;whiteSpace=wrap;html=1;fillColor=#FFFFFF;strokeColor=#000000;fontColor=#000000;fontSize=20;fontStyle=1;align=center;verticalAlign=middle;spacing=8;strokeWidth=2;",
  decision:
    "rhombus;whiteSpace=wrap;html=1;fillColor=#FFFFFF;strokeColor=#000000;fontColor=#000000;fontSize=20;fontStyle=1;align=center;verticalAlign=middle;spacing=10;strokeWidth=2;",
  terminator:
    "ellipse;whiteSpace=wrap;html=1;fillColor=#FFFFFF;strokeColor=#000000;fontColor=#000000;fontSize=22;fontStyle=1;align=center;verticalAlign=middle;spacing=10;strokeWidth=2;",
  note:
    "rounded=1;whiteSpace=wrap;html=1;fillColor=#FFFFFF;strokeColor=#000000;fontColor=#000000;fontSize=18;align=center;verticalAlign=middle;spacing=8;strokeWidth=2;dashed=1;",
  edge:
    "edgeStyle=orthogonalEdgeStyle;rounded=0;orthogonalLoop=1;jettySize=auto;html=1;strokeColor=#000000;strokeWidth=2;endArrow=block;endFill=1;",
  edgeLabel:
    "edgeStyle=orthogonalEdgeStyle;rounded=0;orthogonalLoop=1;jettySize=auto;html=1;strokeColor=#000000;strokeWidth=2;endArrow=block;endFill=1;fontSize=18;labelBackgroundColor=#FFFFFF;",
};

const HIDDEN_POINT =
  "ellipse;whiteSpace=wrap;html=1;fillColor=none;strokeColor=none;opacity=0;resizable=0;movable=0;deletable=0;rotatable=0;";

function createPage(name, build, options = {}) {
  const pageWidth = options.pageWidth || 1600;
  const pageHeight = options.pageHeight || 980;
  const yOffset = options.yOffset ?? 0;
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
  createPage("2.1 系统需求分析", ({ addVertex, addEdge }) => {
    const scene = addVertex({
      value: "农业现场应用需求与场景挑战\n点位分散 / 环境复杂 / 通信条件不稳定",
      x: 450,
      y: 70,
      w: 700,
      h: 96,
      style: STYLES.boxStrong,
    });
    const chain = addVertex({
      value: "系统必须覆盖完整业务链路\n感知 / 传输 / 处理 / 上报 / 维护",
      x: 390,
      y: 240,
      w: 820,
      h: 96,
      style: STYLES.boxStrong,
    });
    const sense = addVertex({
      value: "环境参数采集\n温湿度 / 光照 / 土壤养分",
      x: 20,
      y: 460,
      w: 280,
      h: 108,
      style: STYLES.boxCompact,
    });
    const lora = addVertex({
      value: "多子节点接入\nLoRa 低功耗 / 远距离传输",
      x: 330,
      y: 460,
      w: 280,
      h: 108,
      style: STYLES.boxCompact,
    });
    const network = addVertex({
      value: "远程联网与平台交互\nWiFi / Cat1 / MQTT",
      x: 640,
      y: 460,
      w: 280,
      h: 108,
      style: STYLES.boxCompact,
    });
    const maintain = addVertex({
      value: "部署与维护便利性\nAP 配网 / OTA / NVS 保存",
      x: 950,
      y: 460,
      w: 280,
      h: 108,
      style: STYLES.boxCompact,
    });
    const extend = addVertex({
      value: "智能扩展能力\nK210 害虫识别 / 外设预留",
      x: 1260,
      y: 460,
      w: 280,
      h: 108,
      style: STYLES.boxCompact,
    });
    const outcome = addVertex({
      value: "面向农业现场的综合物联网网关平台\n兼具感知、通信、控制、维护与后续扩展能力",
      x: 360,
      y: 710,
      w: 880,
      h: 100,
      style: STYLES.boxStrong,
    });

    // Invisible anchor points keep the fan-out/fan-in buses perfectly orthogonal.
    const topHub = addVertex({ value: "", x: 799, y: 390, w: 2, h: 2, style: HIDDEN_POINT });
    const top1 = addVertex({ value: "", x: 159, y: 430, w: 2, h: 2, style: HIDDEN_POINT });
    const top2 = addVertex({ value: "", x: 469, y: 430, w: 2, h: 2, style: HIDDEN_POINT });
    const top3 = addVertex({ value: "", x: 779, y: 430, w: 2, h: 2, style: HIDDEN_POINT });
    const top4 = addVertex({ value: "", x: 1089, y: 430, w: 2, h: 2, style: HIDDEN_POINT });
    const top5 = addVertex({ value: "", x: 1399, y: 430, w: 2, h: 2, style: HIDDEN_POINT });

    const bottom1 = addVertex({ value: "", x: 159, y: 610, w: 2, h: 2, style: HIDDEN_POINT });
    const bottom2 = addVertex({ value: "", x: 469, y: 610, w: 2, h: 2, style: HIDDEN_POINT });
    const bottom3 = addVertex({ value: "", x: 779, y: 610, w: 2, h: 2, style: HIDDEN_POINT });
    const bottom4 = addVertex({ value: "", x: 1089, y: 610, w: 2, h: 2, style: HIDDEN_POINT });
    const bottom5 = addVertex({ value: "", x: 1399, y: 610, w: 2, h: 2, style: HIDDEN_POINT });
    const bottomHub = addVertex({ value: "", x: 799, y: 660, w: 2, h: 2, style: HIDDEN_POINT });

    addEdge({ source: scene, target: chain, style: "exitY=1;entryY=0;" });
    addEdge({
      source: chain,
      target: topHub,
      style: "exitX=0.5;exitY=1;entryX=0.5;entryY=0;endArrow=none;",
    });

    [top1, top2, top3, top4, top5].forEach((point) => {
      addEdge({
        source: topHub,
        target: point,
        style: "exitX=0.5;exitY=1;entryX=0.5;entryY=0;endArrow=none;",
      });
    });

    addEdge({ source: top1, target: sense, style: "exitY=1;entryY=0;" });
    addEdge({ source: top2, target: lora, style: "exitY=1;entryY=0;" });
    addEdge({ source: top3, target: network, style: "exitY=1;entryY=0;" });
    addEdge({ source: top4, target: maintain, style: "exitY=1;entryY=0;" });
    addEdge({ source: top5, target: extend, style: "exitY=1;entryY=0;" });

    [
      [sense, bottom1],
      [lora, bottom2],
      [network, bottom3],
      [maintain, bottom4],
      [extend, bottom5],
    ].forEach(([source, target]) => {
      addEdge({
        source,
        target,
        style: "exitX=0.5;exitY=1;entryX=0.5;entryY=0;endArrow=none;",
      });
    });

    [bottom1, bottom2, bottom3, bottom4, bottom5].forEach((point) => {
      addEdge({
        source: point,
        target: bottomHub,
        style: "exitX=0.5;exitY=1;entryX=0.5;entryY=0;endArrow=none;",
      });
    });

    addEdge({ source: bottomHub, target: outcome, style: "exitY=1;entryY=0;" });
  }, { pageHeight: 900, yOffset: 0 })
);

pages.push(
  createPage("2.2 系统总体架构设计", ({ addVertex, addEdge }) => {
    const perceptionLabel = addVertex({
      value: "感知层",
      x: 660,
      y: 50,
      w: 280,
      h: 58,
      style: STYLES.boxStrongCompact,
    });
    const localSensor = addVertex({
      value: "本地土壤与环境感知\n温度 / 湿度 / 光照 / EC / NPK",
      x: 130,
      y: 150,
      w: 380,
      h: 88,
      style: STYLES.boxCompact,
    });
    const loraNode = addVertex({
      value: "LoRa 子节点\n空气温湿度 / 光照采集 / 分布式布点",
      x: 1090,
      y: 150,
      w: 380,
      h: 88,
      style: STYLES.boxCompact,
    });

    const gatewayLabel = addVertex({
      value: "网关层",
      x: 660,
      y: 280,
      w: 280,
      h: 58,
      style: STYLES.boxStrongCompact,
    });
    const supportLeft = addVertex({
      value: "本地支撑模块\nWiFi 配网 / NVS 存储",
      x: 110,
      y: 380,
      w: 320,
      h: 96,
      style: STYLES.boxCompact,
    });
    const gatewayCore = addVertex({
      value: "ESP32 网关核心\n数据汇聚 / 协议转换 / 通信管理 / 任务调度",
      x: 550,
      y: 380,
      w: 500,
      h: 96,
      style: STYLES.boxStrong,
    });
    const supportRight = addVertex({
      value: "通信与维护支撑\nLoRa 通信 / Cat1 备份联网 / OTA 升级",
      x: 1170,
      y: 380,
      w: 320,
      h: 96,
      style: STYLES.boxCompact,
    });
    const mqtt = addVertex({
      value: "MQTT 通信模块\n数据上报 / 指令接收 / 云端交互",
      x: 550,
      y: 560,
      w: 500,
      h: 86,
      style: STYLES.boxStrongCompact,
    });

    const platformLabel = addVertex({
      value: "平台层",
      x: 660,
      y: 700,
      w: 280,
      h: 58,
      style: STYLES.boxStrongCompact,
    });
    const display = addVertex({
      value: "数据展示 / 远程监控",
      x: 130,
      y: 785,
      w: 320,
      h: 82,
      style: STYLES.boxCompact,
    });
    const cloud = addVertex({
      value: "MQTT 云平台\n环境数据汇总 / 状态展示 / 平台业务处理",
      x: 550,
      y: 780,
      w: 500,
      h: 92,
      style: STYLES.boxStrong,
    });
    const control = addVertex({
      value: "指令下发 / 参数配置 / OTA 通知",
      x: 1160,
      y: 785,
      w: 320,
      h: 82,
      style: STYLES.boxCompact,
    });

    const topHub = addVertex({
      value: "",
      x: 799,
      y: 300,
      w: 2,
      h: 2,
      style: HIDDEN_POINT,
    });

    addEdge({
      source: localSensor,
      target: topHub,
      style: "exitX=1;exitY=0.5;entryX=0.5;entryY=0;endArrow=none;",
      points: [{ x: 650, y: 194 }, { x: 650, y: 300 }],
    });
    addEdge({
      source: loraNode,
      target: topHub,
      style: "exitX=0;exitY=0.5;entryX=0.5;entryY=0;endArrow=none;",
      points: [{ x: 950, y: 194 }, { x: 950, y: 300 }],
    });
    addEdge({ source: topHub, target: gatewayCore, style: "exitY=1;entryY=0;" });
    addEdge({
      source: supportLeft,
      target: gatewayCore,
      style: "exitX=1;exitY=0.5;entryX=0;entryY=0.5;",
    });
    addEdge({
      source: supportRight,
      target: gatewayCore,
      style: "exitX=0;exitY=0.5;entryX=1;entryY=0.5;",
    });
    addEdge({ source: gatewayCore, target: mqtt, style: "exitY=1;entryY=0;" });
    addEdge({ source: mqtt, target: cloud, style: "exitY=1;entryY=0;" });
    addEdge({
      source: cloud,
      target: display,
      style: "exitX=0;exitY=0.5;entryX=1;entryY=0.5;endArrow=none;",
    });
    addEdge({
      source: control,
      target: cloud,
      style: "exitX=0;exitY=0.5;entryX=1;entryY=0.5;endArrow=none;",
    });
  }, { pageHeight: 940, yOffset: 0 })
);

pages.push(
  createPage("2.3 系统功能模块划分", ({ addVertex, addEdge }) => {
    const prototype = addVertex({
      value: "原型搭建方式：面包板供电分配 + 杜邦线连接 + ESP32 开发板",
      x: 490,
      y: 40,
      w: 820,
      h: 60,
      style: STYLES.note,
    });
    const node = addVertex({
      value: "农业子节点\n空气温度 / 空气湿度 / 光照采集",
      x: 30,
      y: 155,
      w: 300,
      h: 104,
      style: STYLES.boxStrongCompact,
    });
    const loraChild = addVertex({
      value: "LoRa 模块（MW1268）\n子节点侧\n无线发送 / 状态响应",
      x: 430,
      y: 155,
      w: 290,
      h: 112,
      style: STYLES.boxCompact,
    });
    const loraGateway = addVertex({
      value: "LoRa 模块（MW1268）\n网关侧\nUART 接入 ESP32",
      x: 840,
      y: 155,
      w: 300,
      h: 112,
      style: STYLES.boxCompact,
    });
    const esp32 = addVertex({
      value: "ESP32 智能农业助手\n查询命令发送 / 数据接收 / 校验解析",
      x: 1270,
      y: 145,
      w: 380,
      h: 124,
      style: STYLES.boxStrong,
    });

    const breadboard = addVertex({
      value: "面包板原型连接环境\n供电分配 / 引脚连接 / 接口调整",
      x: 570,
      y: 395,
      w: 430,
      h: 92,
      style: STYLES.note,
    });

    const poll = addVertex({
      value: "节点状态轮询",
      x: 1190,
      y: 450,
      w: 200,
      h: 72,
      style: STYLES.boxCompact,
    });
    const ack = addVertex({
      value: "在线确认",
      x: 1500,
      y: 450,
      w: 180,
      h: 72,
      style: STYLES.boxCompact,
    });
    const receive = addVertex({
      value: "数据帧接收",
      x: 1190,
      y: 585,
      w: 200,
      h: 72,
      style: STYLES.boxCompact,
    });
    const parse = addVertex({
      value: "校验与协议解析",
      x: 1480,
      y: 585,
      w: 200,
      h: 72,
      style: STYLES.boxCompact,
    });
    const upload = addVertex({
      value: "上传到平台侧",
      x: 1485,
      y: 730,
      w: 200,
      h: 76,
      style: STYLES.boxStrongCompact,
    });

    const leftDrop = addVertex({ value: "", x: 574, y: 340, w: 2, h: 2, style: HIDDEN_POINT });
    const midDrop = addVertex({ value: "", x: 782, y: 340, w: 2, h: 2, style: HIDDEN_POINT });
    const rightDrop = addVertex({ value: "", x: 989, y: 340, w: 2, h: 2, style: HIDDEN_POINT });

    const espHubTop = addVertex({ value: "", x: 1459, y: 340, w: 2, h: 2, style: HIDDEN_POINT });
    const espHubMid = addVertex({ value: "", x: 1459, y: 535, w: 2, h: 2, style: HIDDEN_POINT });

    addEdge({
      source: node,
      target: loraChild,
      label: "采集数据",
      style: "exitX=1;exitY=0.5;entryX=0;entryY=0.5;",
    });
    addEdge({
      source: loraChild,
      target: loraGateway,
      label: "LoRa 无线传输",
      style: "exitX=1;exitY=0.5;entryX=0;entryY=0.5;",
    });
    addEdge({
      source: loraGateway,
      target: esp32,
      label: "UART",
      style: "exitX=1;exitY=0.5;entryX=0;entryY=0.5;",
    });

    addEdge({
      source: loraChild,
      target: leftDrop,
      style: "dashed=1;endArrow=none;strokeColor=#000000;exitX=0.5;exitY=1;entryX=0.5;entryY=0;",
    });
    addEdge({
      source: loraGateway,
      target: rightDrop,
      style: "dashed=1;endArrow=none;strokeColor=#000000;exitX=0.5;exitY=1;entryX=0.5;entryY=0;",
    });
    addEdge({
      source: leftDrop,
      target: midDrop,
      style: "dashed=1;endArrow=none;strokeColor=#000000;exitX=1;exitY=0.5;entryX=0;entryY=0.5;",
    });
    addEdge({
      source: rightDrop,
      target: midDrop,
      style: "dashed=1;endArrow=none;strokeColor=#000000;exitX=0;exitY=0.5;entryX=1;entryY=0.5;",
    });
    addEdge({
      source: midDrop,
      target: breadboard,
      style: "dashed=1;endArrow=none;strokeColor=#000000;exitX=0.5;exitY=1;entryX=0.5;entryY=0;",
    });

    addEdge({
      source: esp32,
      target: espHubTop,
      style: "exitX=0.5;exitY=1;entryX=0.5;entryY=0;endArrow=none;",
    });
    addEdge({
      source: espHubTop,
      target: poll,
      style: "exitX=0;exitY=0.5;entryX=0.5;entryY=0;",
    });
    addEdge({
      source: espHubTop,
      target: ack,
      style: "exitX=1;exitY=0.5;entryX=0.5;entryY=0;",
    });
    addEdge({
      source: espHubTop,
      target: espHubMid,
      style: "exitX=0.5;exitY=1;entryX=0.5;entryY=0;endArrow=none;",
    });
    addEdge({
      source: espHubMid,
      target: receive,
      style: "exitX=0;exitY=0.5;entryX=0.5;entryY=0;",
    });
    addEdge({
      source: espHubMid,
      target: parse,
      style: "exitX=1;exitY=0.5;entryX=0.5;entryY=0;",
    });
    addEdge({
      source: parse,
      target: upload,
      style: "exitX=0.5;exitY=1;entryX=0.5;entryY=0;",
    });
  }, { pageWidth: 1800, pageHeight: 900, yOffset: 0 })
);

pages.push(
  createPage("2.4 系统工作流程分析", ({ addVertex, addEdge }) => {
    const start = addVertex({
      value: "系统上电 / 内核启动",
      x: 640,
      y: 50,
      w: 320,
      h: 76,
      style: STYLES.terminator,
    });
    const init = addVertex({
      value: "基础外设初始化\nLED / 按键 / NVS / LoRa / Cat1 / MQTT / 传感器接口",
      x: 360,
      y: 170,
      w: 880,
      h: 96,
      style: STYLES.boxStrong,
    });
    const tasks = addVertex({
      value: "创建 FreeRTOS 后台任务\n采集 / 轮询 / 联网 / 上报 / 升级写入",
      x: 500,
      y: 320,
      w: 600,
      h: 88,
      style: STYLES.boxStrongCompact,
    });
    const collect = addVertex({
      value: "本地采集与节点接入\n环境传感器 / 土壤传感器 / K210 识别结果 / LoRa 子节点状态",
      x: 380,
      y: 450,
      w: 840,
      h: 96,
      style: STYLES.boxCompact,
    });
    const parse = addVertex({
      value: "CRC 校验 / 数据解析 / 统一封装",
      x: 500,
      y: 590,
      w: 600,
      h: 88,
      style: STYLES.boxStrongCompact,
    });
    const chooseNet = addVertex({
      value: "是否存在可用 WiFi 链路？",
      x: 620,
      y: 740,
      w: 360,
      h: 120,
      style: STYLES.decision,
    });
    const wifi = addVertex({
      value: "WiFi 主链路接入\nAP 配网完成后连接云平台",
      x: 170,
      y: 930,
      w: 360,
      h: 92,
      style: STYLES.boxCompact,
    });
    const cat1 = addVertex({
      value: "Cat1 备份链路接入\nWiFi 不可用时建立广域联网",
      x: 1070,
      y: 930,
      w: 360,
      h: 92,
      style: STYLES.boxCompact,
    });
    const wifiDrop = addVertex({ value: "", x: 349, y: 1060, w: 2, h: 2, style: HIDDEN_POINT });
    const cat1Drop = addVertex({ value: "", x: 1249, y: 1060, w: 2, h: 2, style: HIDDEN_POINT });
    const merge = addVertex({ value: "", x: 799, y: 1060, w: 2, h: 2, style: HIDDEN_POINT });
    const mqtt = addVertex({
      value: "MQTT 平台交互\n数据上报 / 远程监控 / 指令与升级任务接收",
      x: 470,
      y: 1110,
      w: 660,
      h: 96,
      style: STYLES.boxStrong,
    });
    const maintain = addVertex({
      value: "OTA 与本地维护\n平台固件更新 / LoRa 子节点升级 / NVS 参数保持",
      x: 430,
      y: 1260,
      w: 740,
      h: 96,
      style: STYLES.boxCompact,
    });
    const running = addVertex({
      value: "系统持续循环运行\n本地采集与控制 + 网关汇聚转换 + 云平台展示与管理",
      x: 360,
      y: 1400,
      w: 880,
      h: 96,
      style: STYLES.boxStrong,
    });

    addEdge({ source: start, target: init, style: "exitY=1;entryY=0;" });
    addEdge({ source: init, target: tasks, style: "exitY=1;entryY=0;" });
    addEdge({ source: tasks, target: collect, style: "exitY=1;entryY=0;" });
    addEdge({ source: collect, target: parse, style: "exitY=1;entryY=0;" });
    addEdge({ source: parse, target: chooseNet, style: "exitY=1;entryY=0;" });
    addEdge({
      source: chooseNet,
      target: wifi,
      label: "是",
      style: "exitX=0;exitY=0.5;entryX=0.5;entryY=0;",
    });
    addEdge({
      source: chooseNet,
      target: cat1,
      label: "否",
      style: "exitX=1;exitY=0.5;entryX=0.5;entryY=0;",
    });
    addEdge({
      source: wifi,
      target: wifiDrop,
      style: "exitX=0.5;exitY=1;entryX=0.5;entryY=0;endArrow=none;",
    });
    addEdge({
      source: cat1,
      target: cat1Drop,
      style: "exitX=0.5;exitY=1;entryX=0.5;entryY=0;endArrow=none;",
    });
    addEdge({
      source: wifiDrop,
      target: merge,
      style: "exitX=1;exitY=0.5;entryX=0;entryY=0.5;endArrow=none;",
    });
    addEdge({
      source: cat1Drop,
      target: merge,
      style: "exitX=0;exitY=0.5;entryX=1;entryY=0.5;endArrow=none;",
    });
    addEdge({ source: merge, target: mqtt, style: "exitY=1;entryY=0;" });
    addEdge({ source: mqtt, target: maintain, style: "exitY=1;entryY=0;" });
    addEdge({ source: maintain, target: running, style: "exitY=1;entryY=0;" });
  }, { pageHeight: 1540, yOffset: 0 })
);

const xml = `<mxfile host="app.diagrams.net" version="24.7.17">
${pages.join("\n")}
</mxfile>
`;

fs.mkdirSync(path.dirname(OUTPUT_PATH), { recursive: true });
fs.writeFileSync(OUTPUT_PATH, xml, "utf8");

console.log(`Generated ${OUTPUT_PATH}`);
