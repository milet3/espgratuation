const fs = require("fs");
const path = require("path");

const inputPath = path.join(__dirname, "..", "docs", "chapter4_flowcharts.drawio");
const outputDir = path.join(__dirname, "..", "docs", "chapter4_drawio_pages");

const xml = fs.readFileSync(inputPath, "utf8");
const mxfileMatch = xml.match(/^<mxfile\b([^>]*)>/);

if (!mxfileMatch) {
  throw new Error("无法识别 mxfile 根节点。");
}

const mxfileAttrs = mxfileMatch[1];
const diagramMatches = [...xml.matchAll(/<diagram\b[^>]*>[\s\S]*?<\/diagram>/g)];

if (diagramMatches.length === 0) {
  throw new Error("未找到任何 diagram 页面。");
}

fs.mkdirSync(outputDir, { recursive: true });

diagramMatches.forEach((match, index) => {
  const pageXml = `<mxfile${mxfileAttrs}>\n  ${match[0]}\n</mxfile>\n`;
  const fileName = `4.${index + 1}.drawio`;
  const outputPath = path.join(outputDir, fileName);
  fs.writeFileSync(outputPath, pageXml, "utf8");
});

console.log(`已拆分 ${diagramMatches.length} 个页面到: ${outputDir}`);
