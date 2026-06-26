import re
import zipfile
import xml.etree.ElementTree as ET
from collections import defaultdict
from pathlib import Path


DOCX_PATH = Path(r"C:\Users\Milet\Desktop\毕设资料\论文.docx")

NS = {
    "w": "http://schemas.openxmlformats.org/wordprocessingml/2006/main",
    "a": "http://schemas.openxmlformats.org/drawingml/2006/main",
    "r": "http://schemas.openxmlformats.org/officeDocument/2006/relationships",
}


def para_text(elem: ET.Element) -> str:
    text = "".join(t.text or "" for t in elem.findall(".//w:t", NS))
    return re.sub(r"\s+", " ", text).strip()


def main() -> None:
    with zipfile.ZipFile(DOCX_PATH) as zf:
        rels_root = ET.fromstring(zf.read("word/_rels/document.xml.rels"))
        rel_map = {}
        for rel in rels_root:
            rid = rel.attrib.get("Id")
            target = rel.attrib.get("Target", "")
            if rid and target.startswith("media/"):
                rel_map[rid] = Path(target).name

        root = ET.fromstring(zf.read("word/document.xml"))

    body = root.find("w:body", NS)
    if body is None:
        return

    items = []
    for child in body:
        if child.tag == f"{{{NS['w']}}}p":
            text = para_text(child)
            embeds = []
            for blip in child.findall(".//a:blip", NS):
                rid = blip.attrib.get(f"{{{NS['r']}}}embed")
                if rid and rid in rel_map:
                    embeds.append(rel_map[rid])
            items.append({"type": "p", "text": text, "images": embeds})
        elif child.tag == f"{{{NS['w']}}}tbl":
            texts = [para_text(p) for p in child.findall(".//w:p", NS)]
            text = " | ".join(t for t in texts if t)
            embeds = []
            for blip in child.findall(".//a:blip", NS):
                rid = blip.attrib.get(f"{{{NS['r']}}}embed")
                if rid and rid in rel_map:
                    embeds.append(rel_map[rid])
            items.append({"type": "tbl", "text": text, "images": embeds})

    image_hits = defaultdict(list)
    for i, item in enumerate(items):
        if not item["images"]:
            continue
        prev_texts = []
        next_texts = []
        for j in range(max(0, i - 3), i):
            if items[j]["text"]:
                prev_texts.append(items[j]["text"])
        for j in range(i + 1, min(len(items), i + 4)):
            if items[j]["text"]:
                next_texts.append(items[j]["text"])
        for image in item["images"]:
            image_hits[image].append(
                {
                    "self_text": item["text"],
                    "prev": prev_texts,
                    "next": next_texts,
                }
            )

    for image in sorted(image_hits):
        print(f"=== {image} ===")
        for hit in image_hits[image]:
            if hit["self_text"]:
                print(f"SELF: {hit['self_text']}")
            if hit["prev"]:
                print("PREV:")
                for line in hit["prev"]:
                    print(f"  - {line}")
            if hit["next"]:
                print("NEXT:")
                for line in hit["next"]:
                    print(f"  - {line}")
            print()


if __name__ == "__main__":
    main()
