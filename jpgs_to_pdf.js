const fs = require('fs');
const path = require('path');

function parseJpegDimensions(buffer) {
  if (buffer.length < 4 || buffer[0] !== 0xff || buffer[1] !== 0xd8) {
    throw new Error('Not a JPEG file');
  }

  let offset = 2;
  const sofMarkers = new Set([
    0xc0, 0xc1, 0xc2, 0xc3,
    0xc5, 0xc6, 0xc7,
    0xc9, 0xca, 0xcb,
    0xcd, 0xce, 0xcf,
  ]);

  while (offset < buffer.length) {
    if (buffer[offset] !== 0xff) {
      offset += 1;
      continue;
    }

    let marker = buffer[offset + 1];
    while (marker === 0xff) {
      offset += 1;
      marker = buffer[offset + 1];
    }

    if (marker === 0xd8 || marker === 0xd9) {
      offset += 2;
      continue;
    }

    if (marker >= 0xd0 && marker <= 0xd7) {
      offset += 2;
      continue;
    }

    const segmentLength = buffer.readUInt16BE(offset + 2);
    if (sofMarkers.has(marker)) {
      const height = buffer.readUInt16BE(offset + 5);
      const width = buffer.readUInt16BE(offset + 7);
      return { width, height };
    }

    offset += 2 + segmentLength;
  }

  throw new Error('JPEG dimensions not found');
}

function toBuffer(value) {
  return Buffer.isBuffer(value) ? value : Buffer.from(String(value), 'binary');
}

function buildPdf(imagePaths, outputPath) {
  const objects = [];
  const addObject = (content) => {
    objects.push(toBuffer(content));
    return objects.length;
  };

  addObject('<< /Type /Catalog /Pages 2 0 R >>');
  addObject('<<>>');

  const pageObjectIds = [];

  for (let i = 0; i < imagePaths.length; i += 1) {
    const imagePath = imagePaths[i];
    const imageBytes = fs.readFileSync(imagePath);
    const { width, height } = parseJpegDimensions(imageBytes);

    const imageObjectId = addObject(
      Buffer.concat([
        Buffer.from(
          `<< /Type /XObject /Subtype /Image /Width ${width} /Height ${height} /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode /Length ${imageBytes.length} >>\nstream\n`,
          'binary'
        ),
        imageBytes,
        Buffer.from('\nendstream', 'binary'),
      ])
    );

    const contentStream = `q\n${width} 0 0 ${height} 0 0 cm\n/Im${i + 1} Do\nQ`;
    const contentObjectId = addObject(
      `<< /Length ${Buffer.byteLength(contentStream, 'binary')} >>\nstream\n${contentStream}\nendstream`
    );

    const pageObjectId = addObject(
      `<< /Type /Page /Parent 2 0 R /MediaBox [0 0 ${width} ${height}] /Resources << /XObject << /Im${i + 1} ${imageObjectId} 0 R >> /ProcSet [/PDF /ImageC] >> /Contents ${contentObjectId} 0 R >>`
    );

    pageObjectIds.push(pageObjectId);
  }

  objects[1] = Buffer.from(
    `<< /Type /Pages /Count ${pageObjectIds.length} /Kids [${pageObjectIds.map((id) => `${id} 0 R`).join(' ')}] >>`,
    'binary'
  );

  const chunks = [Buffer.from('%PDF-1.4\n%\xff\xff\xff\xff\n', 'binary')];
  const offsets = [0];
  let position = chunks[0].length;

  for (let i = 0; i < objects.length; i += 1) {
    offsets.push(position);
    const header = Buffer.from(`${i + 1} 0 obj\n`, 'binary');
    const footer = Buffer.from('\nendobj\n', 'binary');
    chunks.push(header, objects[i], footer);
    position += header.length + objects[i].length + footer.length;
  }

  const xrefOffset = position;
  const xrefLines = ['xref', `0 ${objects.length + 1}`, '0000000000 65535 f '];
  for (let i = 1; i < offsets.length; i += 1) {
    xrefLines.push(`${String(offsets[i]).padStart(10, '0')} 00000 n `);
  }

  const trailer = [
    'trailer',
    `<< /Size ${objects.length + 1} /Root 1 0 R >>`,
    'startxref',
    String(xrefOffset),
    '%%EOF',
  ].join('\n');

  chunks.push(Buffer.from(`${xrefLines.join('\n')}\n${trailer}`, 'binary'));
  fs.writeFileSync(outputPath, Buffer.concat(chunks));
}

function main() {
  const [, , outputPath, ...imagePaths] = process.argv;
  if (!outputPath || imagePaths.length === 0) {
    console.error(`Usage: node ${path.basename(__filename)} <output.pdf> <page1.jpg> ...`);
    process.exit(1);
  }

  buildPdf(imagePaths, outputPath);
  console.log(outputPath);
}

main();
