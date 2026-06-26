from __future__ import annotations

import os
import zipfile
from datetime import datetime, timezone
from pathlib import Path
from xml.sax.saxutils import escape


SLIDE_W = 12192000
SLIDE_H = 6858000

WORKDIR = Path(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2")
COVER_IMAGE = WORKDIR / "cover_from_clipboard.png"
GATEWAY_IMAGE = Path(r"C:\Users\Milet\Desktop\毕设资料\网关.jpg")
NODE_IMAGE = Path(r"C:\Users\Milet\Desktop\毕设资料\节点.jpg")
AP_PAGE_IMAGE = Path(r"C:\Users\Milet\Desktop\毕设资料\配网页面.jpg")
OUTPUT_PPTX = WORKDIR / "thesis_defense_ppt_15_gdou_cover_ap_lora_photos_params.pptx"


def emu(inches: float) -> int:
    return int(inches * 914400)


def rgb_fill(color: str, alpha: int | None = None) -> str:
    if alpha is None:
        return f"<a:solidFill><a:srgbClr val=\"{color}\"/></a:solidFill>"
    return (
        f"<a:solidFill><a:srgbClr val=\"{color}\">"
        f"<a:alpha val=\"{alpha}\"/></a:srgbClr></a:solidFill>"
    )


def line_fill(color: str | None = None, width: int = 12700) -> str:
    if color is None:
        return "<a:ln><a:noFill/></a:ln>"
    return (
        f"<a:ln w=\"{width}\">"
        f"<a:solidFill><a:srgbClr val=\"{color}\"/></a:solidFill>"
        f"</a:ln>"
    )


def paragraph_xml(
    text: str,
    size: int = 1800,
    color: str = "2A2A2A",
    bold: bool = False,
    align: str = "l",
    font: str = "Microsoft YaHei",
) -> str:
    text = escape(text)
    bold_attr = " b=\"1\"" if bold else ""
    return (
        f"<a:p><a:pPr algn=\"{align}\"/>"
        f"<a:r><a:rPr lang=\"zh-CN\" sz=\"{size}\"{bold_attr}>"
        f"<a:solidFill><a:srgbClr val=\"{color}\"/></a:solidFill>"
        f"<a:latin typeface=\"{font}\"/><a:ea typeface=\"{font}\"/><a:cs typeface=\"{font}\"/>"
        f"</a:rPr><a:t>{text}</a:t></a:r>"
        f"<a:endParaRPr lang=\"zh-CN\" sz=\"{size}\"/></a:p>"
    )


def textbox_xml(
    shape_id: int,
    name: str,
    x: int,
    y: int,
    cx: int,
    cy: int,
    paragraphs: list[str],
    *,
    fill: str | None = None,
    line: str | None = None,
    tx_box: bool = True,
    inset: tuple[int, int, int, int] = (91440, 45720, 91440, 45720),
    anchor: str = "ctr",
) -> str:
    fill_xml = "<a:noFill/>" if fill is None else fill
    line_xml = "<a:ln><a:noFill/></a:ln>" if line is None else line
    tx_attr = " txBox=\"1\"" if tx_box else ""
    l_ins, t_ins, r_ins, b_ins = inset
    body = "".join(paragraphs) if paragraphs else "<a:p/>"
    return (
        f"<p:sp>"
        f"<p:nvSpPr><p:cNvPr id=\"{shape_id}\" name=\"{escape(name)}\"/>"
        f"<p:cNvSpPr{tx_attr}/><p:nvPr/></p:nvSpPr>"
        f"<p:spPr><a:xfrm><a:off x=\"{x}\" y=\"{y}\"/><a:ext cx=\"{cx}\" cy=\"{cy}\"/></a:xfrm>"
        f"<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom>{fill_xml}{line_xml}</p:spPr>"
        f"<p:txBody><a:bodyPr wrap=\"square\" lIns=\"{l_ins}\" tIns=\"{t_ins}\" rIns=\"{r_ins}\" bIns=\"{b_ins}\" anchor=\"{anchor}\"/>"
        f"<a:lstStyle/>{body}</p:txBody>"
        f"</p:sp>"
    )


def picture_xml(shape_id: int, x: int, y: int, cx: int, cy: int, rel_id: str) -> str:
    return (
        f"<p:pic>"
        f"<p:nvPicPr><p:cNvPr id=\"{shape_id}\" name=\"Picture {shape_id}\"/>"
        f"<p:cNvPicPr><a:picLocks noChangeAspect=\"1\"/></p:cNvPicPr><p:nvPr/></p:nvPicPr>"
        f"<p:blipFill><a:blip r:embed=\"{rel_id}\"/><a:stretch><a:fillRect/></a:stretch></p:blipFill>"
        f"<p:spPr><a:xfrm><a:off x=\"{x}\" y=\"{y}\"/><a:ext cx=\"{cx}\" cy=\"{cy}\"/></a:xfrm>"
        f"<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></p:spPr>"
        f"</p:pic>"
    )


def slide_xml(elements: list[str]) -> str:
    sp_tree = (
        "<p:spTree>"
        "<p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr>"
        "<p:grpSpPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"0\" cy=\"0\"/>"
        "<a:chOff x=\"0\" y=\"0\"/><a:chExt cx=\"0\" cy=\"0\"/></a:xfrm></p:grpSpPr>"
        + "".join(elements)
        + "</p:spTree>"
    )
    return (
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<p:sld xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
        "xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\">"
        f"<p:cSld>{sp_tree}</p:cSld>"
        "<p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr>"
        "</p:sld>"
    )


def footer_elements(shape_base_id: int, page_label: str) -> list[str]:
    return [
        textbox_xml(
            shape_base_id,
            "FooterLine",
            emu(0.45),
            emu(7.02),
            emu(10.6),
            emu(0.02),
            [],
            fill=rgb_fill("6EC9A4"),
            line=line_fill(None),
            tx_box=False,
        ),
        textbox_xml(
            shape_base_id + 1,
            "PageLabel",
            emu(11.1),
            emu(6.75),
            emu(1.6),
            emu(0.35),
            [paragraph_xml(page_label, size=1100, color="5A6A85", align="r")],
            inset=(0, 0, 0, 0),
        ),
    ]


def body_slide(title: str, bullets: list[str], page_label: str) -> str:
    elements: list[str] = []
    elements.append(
        textbox_xml(
            2,
            "TopBand",
            0,
            0,
            SLIDE_W,
            emu(0.72),
            [],
            fill=rgb_fill("1F5EAB"),
            line=line_fill(None),
            tx_box=False,
        )
    )
    elements.append(
        textbox_xml(
            3,
            "AccentBar",
            emu(0.55),
            emu(0.22),
            emu(0.12),
            emu(0.28),
            [],
            fill=rgb_fill("71C8A6"),
            line=line_fill(None),
            tx_box=False,
        )
    )
    elements.append(
        textbox_xml(
            4,
            "Title",
            emu(0.8),
            emu(0.09),
            emu(7.2),
            emu(0.45),
            [paragraph_xml(title, size=2600, color="FFFFFF", bold=True)],
            inset=(0, 0, 0, 0),
        )
    )
    bullet_paragraphs = [paragraph_xml(f"- {item}", size=1900, color="2C2C2C") for item in bullets]
    elements.append(
        textbox_xml(
            5,
            "ContentBox",
            emu(0.75),
            emu(1.1),
            emu(11.75),
            emu(5.35),
            bullet_paragraphs,
            fill=rgb_fill("FFFFFF"),
            line=line_fill("D7E3F3"),
            inset=(emu(0.22), emu(0.12), emu(0.22), emu(0.12)),
            anchor="t",
        )
    )
    elements.extend(footer_elements(6, page_label))
    return slide_xml(elements)


def section_slide(title: str, subtitle: str, page_label: str, number_text: str) -> str:
    elements: list[str] = []
    elements.append(picture_xml(2, 0, 0, SLIDE_W, SLIDE_H, "rId2"))
    elements.append(
        textbox_xml(
            3,
            "LeftOverlay",
            emu(0.0),
            emu(0.0),
            emu(5.0),
            emu(7.5),
            [],
            fill=rgb_fill("103D73", alpha=74000),
            line=line_fill(None),
            tx_box=False,
        )
    )
    elements.append(
        textbox_xml(
            4,
            "GreenBar",
            emu(0.7),
            emu(1.15),
            emu(0.16),
            emu(1.45),
            [],
            fill=rgb_fill("76D0AB"),
            line=line_fill(None),
            tx_box=False,
        )
    )
    elements.append(
        textbox_xml(
            5,
            "SectionNo",
            emu(0.95),
            emu(0.9),
            emu(1.2),
            emu(0.9),
            [paragraph_xml(number_text, size=3000, color="D8EBFF", bold=True)],
            inset=(0, 0, 0, 0),
        )
    )
    elements.append(
        textbox_xml(
            6,
            "SectionTitle",
            emu(0.95),
            emu(2.0),
            emu(3.35),
            emu(1.15),
            [
                paragraph_xml(title, size=2800, color="FFFFFF", bold=True),
                paragraph_xml(subtitle, size=1350, color="D6E6F9"),
            ],
            inset=(0, 0, 0, 0),
        )
    )
    elements.extend(footer_elements(7, page_label))
    return slide_xml(elements)


def split_panels_slide(
    title: str,
    left_title: str,
    left_items: list[str],
    right_title: str,
    right_items: list[str],
    page_label: str,
) -> str:
    elements: list[str] = []
    elements.append(
        textbox_xml(2, "TopBand", 0, 0, SLIDE_W, emu(0.72), [], fill=rgb_fill("1F5EAB"), line=line_fill(None), tx_box=False)
    )
    elements.append(
        textbox_xml(3, "Title", emu(0.8), emu(0.09), emu(7.0), emu(0.45), [paragraph_xml(title, size=2600, color="FFFFFF", bold=True)], inset=(0, 0, 0, 0))
    )
    panel_specs = [
        (4, emu(0.7), left_title, left_items, "1F5EAB", "EAF3FF"),
        (5, emu(6.45), right_title, right_items, "38A37A", "F0FAF5"),
    ]
    for sid, x, panel_title, items, accent, fill_color in panel_specs:
        paras = [paragraph_xml(panel_title, size=2100, color=accent, bold=True), paragraph_xml("")]
        paras += [paragraph_xml(f"- {item}", size=1750, color="2B2B2B") for item in items]
        elements.append(
            textbox_xml(
                sid,
                f"Panel{sid}",
                x,
                emu(1.25),
                emu(5.0),
                emu(4.95),
                paras,
                fill=rgb_fill(fill_color),
                line=line_fill("D7E3F3", width=19050),
                inset=(emu(0.2), emu(0.16), emu(0.2), emu(0.12)),
                anchor="t",
            )
        )
        elements.append(
            textbox_xml(
                sid + 10,
                f"Accent{sid}",
                x,
                emu(1.25),
                emu(0.12),
                emu(4.95),
                [],
                fill=rgb_fill(accent),
                line=line_fill(None),
                tx_box=False,
            )
        )
    elements.extend(footer_elements(30, page_label))
    return slide_xml(elements)


def cards_slide(title: str, cards: list[tuple[str, list[str], str, str]], page_label: str) -> str:
    elements: list[str] = []
    elements.append(
        textbox_xml(2, "TopBand", 0, 0, SLIDE_W, emu(0.72), [], fill=rgb_fill("1F5EAB"), line=line_fill(None), tx_box=False)
    )
    elements.append(
        textbox_xml(3, "Title", emu(0.8), emu(0.09), emu(7.0), emu(0.45), [paragraph_xml(title, size=2600, color="FFFFFF", bold=True)], inset=(0, 0, 0, 0))
    )
    x_positions = [emu(0.6), emu(4.25), emu(7.9)]
    widths = [emu(3.35), emu(3.35), emu(3.35)]
    for idx, (card_title, items, line_color, fill_color) in enumerate(cards):
        x = x_positions[idx]
        w = widths[idx]
        sid = 4 + idx * 2
        paras = [paragraph_xml(card_title, size=2100, color=line_color, bold=True, align="ctr"), paragraph_xml("")]
        paras += [paragraph_xml(f"- {item}", size=1550, color="2D2D2D") for item in items]
        elements.append(
            textbox_xml(
                sid,
                f"Card{idx}",
                x,
                emu(1.45),
                w,
                emu(4.85),
                paras,
                fill=rgb_fill(fill_color),
                line=line_fill(line_color, width=19050),
                inset=(emu(0.14), emu(0.14), emu(0.14), emu(0.14)),
                anchor="t",
            )
        )
        elements.append(
            textbox_xml(
                sid + 1,
                f"CardTop{idx}",
                x,
                emu(1.45),
                w,
                emu(0.18),
                [],
                fill=rgb_fill(line_color),
                line=line_fill(None),
                tx_box=False,
            )
        )
    elements.extend(footer_elements(20, page_label))
    return slide_xml(elements)


def agenda_photo_slide(page_label: str) -> str:
    elements: list[str] = []
    elements.append(
        textbox_xml(2, "Background", 0, 0, SLIDE_W, SLIDE_H, [], fill=rgb_fill("F7F3EC"), line=line_fill(None), tx_box=False)
    )
    elements.append(
        textbox_xml(3, "RightBase", emu(7.0), 0, emu(6.35), SLIDE_H, [], fill=rgb_fill("103D73"), line=line_fill(None), tx_box=False)
    )
    elements.append(picture_xml(4, emu(7.2), 0, emu(4.8), SLIDE_H, "rId2"))
    elements.append(
        textbox_xml(5, "ImageOverlay", emu(7.2), 0, emu(4.8), SLIDE_H, [], fill=rgb_fill("103D73", alpha=36000), line=line_fill(None), tx_box=False)
    )
    elements.append(
        textbox_xml(
            6,
            "Eyebrow",
            emu(0.8),
            emu(0.72),
            emu(2.8),
            emu(0.3),
            [paragraph_xml("PROJECT INTRO", size=1050, color="6D87A1", bold=True)],
            inset=(0, 0, 0, 0),
        )
    )
    elements.append(
        textbox_xml(
            7,
            "Title",
            emu(0.8),
            emu(1.02),
            emu(4.8),
            emu(0.72),
            [paragraph_xml("作品目的与设计选择", size=2850, color="173E69", bold=True)],
            inset=(0, 0, 0, 0),
        )
    )
    elements.append(
        textbox_xml(
            8,
            "Desc",
            emu(0.82),
            emu(1.72),
            emu(5.9),
            emu(0.72),
            [paragraph_xml("这一页主要把“为什么做、做成了什么、为什么选 LoRa”这三个答辩开场问题先讲清楚", size=1380, color="586A7C")],
            inset=(0, 0, 0, 0),
        )
    )
    sections = [
        ("01", "作品目的", "面向农业现场，把土壤、空气和 LoRa 节点数据采上来，再通过网关稳定上传到平台，同时兼顾后期远程维护", "1F5EAB", "EAF3FF"),
        ("02", "我已完成的功能", "完成土壤采集、网关空气采集、LoRa 子节点接入、WiFi AP 配网、Cat1 备份联网、MQTT 上报、NVS 存储和网关本体 OTA", "38A37A", "EFFAF5"),
        ("03", "为什么选择 LoRa", "相比 WiFi、蓝牙等方式，LoRa 更适合农业现场分散节点，优势是低功耗、距离远、布点灵活，而且对现场网络覆盖依赖更小", "C8872D", "FFF5E8"),
    ]
    for idx, (num, heading, desc, accent, fill_color) in enumerate(sections):
        y = emu(2.55 + idx * 1.3)
        sid = 10 + idx * 3
        elements.append(
            textbox_xml(
                sid,
                f"SectionNo{idx}",
                emu(0.82),
                y,
                emu(0.78),
                emu(0.78),
                [paragraph_xml(num, size=2200, color="FFFFFF", bold=True, align="ctr")],
                fill=rgb_fill(accent),
                line=line_fill(None),
                inset=(0, 0, 0, 0),
                anchor="ctr",
            )
        )
        elements.append(
            textbox_xml(
                sid + 1,
                f"SectionCard{idx}",
                emu(1.78),
                y - emu(0.05),
                emu(4.95),
                emu(0.95),
                [
                    paragraph_xml(heading, size=1850, color=accent, bold=True),
                    paragraph_xml(desc, size=1280, color="425464"),
                ],
                fill=rgb_fill(fill_color),
                line=line_fill(accent, width=19050),
                inset=(emu(0.14), emu(0.1), emu(0.14), emu(0.08)),
                anchor="ctr",
            )
        )
        elements.append(
            textbox_xml(
                sid + 2,
                f"SectionAccent{idx}",
                emu(1.78),
                y - emu(0.05),
                emu(0.16),
                emu(0.95),
                [],
                fill=rgb_fill(accent),
                line=line_fill(None),
                tx_box=False,
            )
        )
    elements.append(
        textbox_xml(
            25,
            "KeywordTitle",
            emu(7.58),
            emu(5.0),
            emu(2.8),
            emu(0.35),
            [paragraph_xml("KEY POINTS", size=1050, color="C7E0F7", bold=True)],
            inset=(0, 0, 0, 0),
        )
    )
    elements.append(
        textbox_xml(
            26,
            "Keywords",
            emu(7.58),
            emu(5.38),
            emu(3.4),
            emu(1.25),
            [
                paragraph_xml("多源采集", size=1850, color="FFFFFF", bold=True),
                paragraph_xml("LoRa 低功耗远距离", size=1850, color="FFFFFF", bold=True),
                paragraph_xml("AP 配网 / Cat1 备份 / OTA", size=1500, color="D7E8F9"),
            ],
            inset=(0, 0, 0, 0),
        )
    )
    elements.extend(footer_elements(30, page_label))
    return slide_xml(elements)


def focus_quote_slide(
    title: str,
    eyebrow: str,
    highlight: str,
    bullets: list[str],
    note: str,
    page_label: str,
    left_fill: str,
    accent: str,
) -> str:
    elements: list[str] = []
    elements.append(
        textbox_xml(2, "Background", 0, 0, SLIDE_W, SLIDE_H, [], fill=rgb_fill("F7FAFE"), line=line_fill(None), tx_box=False)
    )
    elements.append(
        textbox_xml(3, "LeftPanel", emu(0.58), emu(0.88), emu(4.05), emu(5.72), [], fill=rgb_fill(left_fill), line=line_fill(None), tx_box=False)
    )
    elements.append(
        textbox_xml(4, "AccentBar", emu(0.76), emu(1.08), emu(0.88), emu(0.12), [], fill=rgb_fill(accent), line=line_fill(None), tx_box=False)
    )
    highlight_paragraphs = [paragraph_xml(eyebrow, size=1100, color="D8E9F8", bold=True), paragraph_xml("")]
    highlight_paragraphs += [paragraph_xml(line, size=2500, color="FFFFFF", bold=True) for line in highlight.split("\n")]
    elements.append(
        textbox_xml(
            5,
            "Highlight",
            emu(0.82),
            emu(1.35),
            emu(3.5),
            emu(3.4),
            highlight_paragraphs,
            inset=(0, 0, 0, 0),
            anchor="ctr",
        )
    )
    elements.append(
        textbox_xml(
            6,
            "Title",
            emu(5.0),
            emu(0.98),
            emu(5.9),
            emu(0.7),
            [paragraph_xml(title, size=2700, color="173E69", bold=True)],
            inset=(0, 0, 0, 0),
        )
    )
    elements.append(
        textbox_xml(
            7,
            "TitleLine",
            emu(5.0),
            emu(1.62),
            emu(1.45),
            emu(0.05),
            [],
            fill=rgb_fill(accent),
            line=line_fill(None),
            tx_box=False,
        )
    )
    bullet_paragraphs = [paragraph_xml("核心要点", size=2100, color=accent, bold=True), paragraph_xml("")]
    bullet_paragraphs += [paragraph_xml(f"- {item}", size=1620, color="2E2E2E") for item in bullets]
    elements.append(
        textbox_xml(
            8,
            "BulletCard",
            emu(5.0),
            emu(1.95),
            emu(6.15),
            emu(3.65),
            bullet_paragraphs,
            fill=rgb_fill("FFFFFF"),
            line=line_fill("D5E0EC", width=19050),
            inset=(emu(0.18), emu(0.14), emu(0.18), emu(0.12)),
            anchor="t",
        )
    )
    elements.append(
        textbox_xml(
            9,
            "NoteCard",
            emu(5.0),
            emu(5.8),
            emu(6.15),
            emu(0.72),
            [paragraph_xml(note, size=1400, color="456176")],
            fill=rgb_fill("EEF6FC"),
            line=line_fill(accent, width=12700),
            inset=(emu(0.14), emu(0.08), emu(0.14), emu(0.06)),
            anchor="ctr",
        )
    )
    elements.extend(footer_elements(20, page_label))
    return slide_xml(elements)


def band_story_slide(title: str, items: list[tuple[str, str, str]], page_label: str) -> str:
    elements: list[str] = []
    elements.append(
        textbox_xml(2, "Background", 0, 0, SLIDE_W, SLIDE_H, [], fill=rgb_fill("FBFCFE"), line=line_fill(None), tx_box=False)
    )
    elements.append(
        textbox_xml(
            3,
            "Title",
            emu(0.8),
            emu(0.85),
            emu(5.4),
            emu(0.75),
            [paragraph_xml(title, size=2800, color="163B66", bold=True)],
            inset=(0, 0, 0, 0),
        )
    )
    elements.append(
        textbox_xml(
            4,
            "TitleAccent",
            emu(0.82),
            emu(1.52),
            emu(1.25),
            emu(0.06),
            [],
            fill=rgb_fill("76D0AB"),
            line=line_fill(None),
            tx_box=False,
        )
    )
    for idx, (label, desc, accent) in enumerate(items):
        y = emu(1.95 + idx * 1.42)
        sid = 10 + idx * 4
        elements.append(
            textbox_xml(
                sid,
                f"ItemNo{idx}",
                emu(0.82),
                y + emu(0.08),
                emu(0.64),
                emu(0.64),
                [paragraph_xml(str(idx + 1), size=1700, color="FFFFFF", bold=True, align="ctr")],
                fill=rgb_fill(accent),
                line=line_fill(None),
                inset=(0, 0, 0, 0),
                anchor="ctr",
            )
        )
        elements.append(
            textbox_xml(
                sid + 1,
                f"ItemLabel{idx}",
                emu(1.62),
                y,
                emu(2.15),
                emu(0.85),
                [paragraph_xml(label, size=1850, color="FFFFFF", bold=True, align="ctr")],
                fill=rgb_fill(accent),
                line=line_fill(None),
                inset=(emu(0.08), emu(0.04), emu(0.08), emu(0.04)),
                anchor="ctr",
            )
        )
        elements.append(
            textbox_xml(
                sid + 2,
                f"ItemDesc{idx}",
                emu(3.92),
                y,
                emu(7.1),
                emu(0.85),
                [paragraph_xml(desc, size=1380, color="33485B")],
                fill=rgb_fill("FFFFFF"),
                line=line_fill(accent, width=19050),
                inset=(emu(0.16), emu(0.08), emu(0.16), emu(0.06)),
                anchor="ctr",
            )
        )
        elements.append(
            textbox_xml(
                sid + 3,
                f"ItemTail{idx}",
                emu(11.14),
                y,
                emu(0.12),
                emu(0.85),
                [],
                fill=rgb_fill(accent),
                line=line_fill(None),
                tx_box=False,
            )
        )
    elements.extend(footer_elements(30, page_label))
    return slide_xml(elements)


def mosaic_slide(title: str, blocks: list[tuple[str, list[str], str, str]], page_label: str) -> str:
    if len(blocks) != 3:
        raise ValueError("mosaic_slide requires exactly 3 blocks")

    elements: list[str] = []
    elements.append(
        textbox_xml(2, "Background", 0, 0, SLIDE_W, SLIDE_H, [], fill=rgb_fill("F4F7FB"), line=line_fill(None), tx_box=False)
    )
    elements.append(
        textbox_xml(
            3,
            "Title",
            emu(0.8),
            emu(0.82),
            emu(5.5),
            emu(0.72),
            [paragraph_xml(title, size=2800, color="173E69", bold=True)],
            inset=(0, 0, 0, 0),
        )
    )
    layouts = [
        (4, emu(0.78), emu(1.6), emu(4.2), emu(4.75)),
        (8, emu(5.23), emu(1.6), emu(5.95), emu(2.0)),
        (12, emu(5.23), emu(3.9), emu(5.95), emu(2.45)),
    ]
    for (sid, x, y, cx, cy), (block_title, items, accent, fill_color) in zip(layouts, blocks):
        paras = [paragraph_xml(block_title, size=2200, color=accent, bold=True), paragraph_xml("")]
        paras += [paragraph_xml(f"- {item}", size=1550, color="2F2F2F") for item in items]
        elements.append(
            textbox_xml(
                sid,
                f"Block{sid}",
                x,
                y,
                cx,
                cy,
                paras,
                fill=rgb_fill(fill_color),
                line=line_fill(accent, width=19050),
                inset=(emu(0.18), emu(0.16), emu(0.18), emu(0.12)),
                anchor="t",
            )
        )
        elements.append(
            textbox_xml(
                sid + 1,
                f"BlockAccent{sid}",
                x,
                y,
                cx,
                emu(0.16),
                [],
                fill=rgb_fill(accent),
                line=line_fill(None),
                tx_box=False,
            )
        )
    elements.extend(footer_elements(20, page_label))
    return slide_xml(elements)


def hardware_photo_slide(page_label: str) -> str:
    elements: list[str] = []
    elements.append(
        textbox_xml(2, "Background", 0, 0, SLIDE_W, SLIDE_H, [], fill=rgb_fill("F4F7FB"), line=line_fill(None), tx_box=False)
    )
    elements.append(
        textbox_xml(
            3,
            "Title",
            emu(0.8),
            emu(0.82),
            emu(5.8),
            emu(0.72),
            [paragraph_xml("硬件设计与实物展示", size=2800, color="173E69", bold=True)],
            inset=(0, 0, 0, 0),
        )
    )
    elements.append(
        textbox_xml(
            4,
            "Desc",
            emu(0.82),
            emu(1.45),
            emu(6.4),
            emu(0.46),
            [paragraph_xml("这一页把网关和子节点的实物原型直接展示出来，答辩时会比单纯列硬件模块更直观", size=1320, color="5E7184")],
            inset=(0, 0, 0, 0),
        )
    )

    elements.append(
        textbox_xml(
            5,
            "GatewayCard",
            emu(0.82),
            emu(1.95),
            emu(5.15),
            emu(4.45),
            [],
            fill=rgb_fill("FFFFFF"),
            line=line_fill("1F5EAB", width=19050),
            tx_box=False,
        )
    )
    elements.append(
        textbox_xml(
            6,
            "GatewayAccent",
            emu(0.82),
            emu(1.95),
            emu(5.15),
            emu(0.16),
            [],
            fill=rgb_fill("1F5EAB"),
            line=line_fill(None),
            tx_box=False,
        )
    )
    elements.append(picture_xml(7, emu(1.05), emu(2.22), emu(4.55), emu(3.4), "rId2"))
    elements.append(
        textbox_xml(
            8,
            "GatewayCaption",
            emu(1.0),
            emu(5.72),
            emu(4.65),
            emu(0.46),
            [
                paragraph_xml("网关实物", size=1750, color="1F5EAB", bold=True, align="ctr"),
                paragraph_xml("ESP32 + LoRa + Cat1 + 土壤/空气采集接口", size=1080, color="556A7D", align="ctr"),
            ],
            inset=(0, 0, 0, 0),
            anchor="ctr",
        )
    )

    elements.append(
        textbox_xml(
            9,
            "NodeCard",
            emu(6.22),
            emu(1.95),
            emu(3.25),
            emu(4.45),
            [],
            fill=rgb_fill("FFFFFF"),
            line=line_fill("38A37A", width=19050),
            tx_box=False,
        )
    )
    elements.append(
        textbox_xml(
            10,
            "NodeAccent",
            emu(6.22),
            emu(1.95),
            emu(3.25),
            emu(0.16),
            [],
            fill=rgb_fill("38A37A"),
            line=line_fill(None),
            tx_box=False,
        )
    )
    elements.append(picture_xml(11, emu(6.55), emu(2.18), emu(2.45), emu(3.28), "rId3"))
    elements.append(
        textbox_xml(
            12,
            "NodeCaption",
            emu(6.4),
            emu(5.72),
            emu(2.95),
            emu(0.46),
            [
                paragraph_xml("LoRa 子节点实物", size=1700, color="38A37A", bold=True, align="ctr"),
                paragraph_xml("节点侧传感器采集与无线回传", size=1040, color="556A7D", align="ctr"),
            ],
            inset=(0, 0, 0, 0),
            anchor="ctr",
        )
    )

    elements.append(
        textbox_xml(
            13,
            "RightTopBox",
            emu(9.78),
            emu(1.95),
            emu(2.5),
            emu(1.9),
            [
                paragraph_xml("答辩时可强调", size=1850, color="C8872D", bold=True),
                paragraph_xml(""),
                paragraph_xml("- 网关负责数据汇聚、联网和平台交互", size=1220, color="394D5E"),
                paragraph_xml("- 子节点负责前端感知和 LoRa 无线回传", size=1220, color="394D5E"),
                paragraph_xml("- 当前原型以实物联调为主，更突出链路可行性验证", size=1220, color="394D5E"),
            ],
            fill=rgb_fill("FFF5E8"),
            line=line_fill("C8872D", width=19050),
            inset=(emu(0.12), emu(0.1), emu(0.12), emu(0.08)),
            anchor="t",
        )
    )
    elements.append(
        textbox_xml(
            14,
            "RightBottomBox",
            emu(9.78),
            emu(4.03),
            emu(2.5),
            emu(2.37),
            [
                paragraph_xml("LoRa 关键参数", size=1850, color="1F5EAB", bold=True),
                paragraph_xml(""),
                paragraph_xml("- 模块型号：MW1268", size=1220, color="394D5E"),
                paragraph_xml("- 接口方式：UART 与 ESP32 连接", size=1220, color="394D5E"),
                paragraph_xml("- 当前模式：透明传输模式", size=1220, color="394D5E"),
                paragraph_xml("- 串口参数：115200 bps，兼容 9600 探测初始化", size=1180, color="394D5E"),
                paragraph_xml("- 当前工程通道：Ch 23", size=1220, color="394D5E"),
                paragraph_xml("- 选择原因：低功耗、远距离、适合农业分散布点", size=1180, color="394D5E"),
            ],
            fill=rgb_fill("EAF3FF"),
            line=line_fill("1F5EAB", width=19050),
            inset=(emu(0.12), emu(0.1), emu(0.12), emu(0.08)),
            anchor="t",
        )
    )
    elements.extend(footer_elements(20, page_label))
    return slide_xml(elements)


def process_slide(title: str, steps: list[tuple[str, str]], page_label: str) -> str:
    elements: list[str] = []
    elements.append(
        textbox_xml(2, "TopBand", 0, 0, SLIDE_W, emu(0.72), [], fill=rgb_fill("1F5EAB"), line=line_fill(None), tx_box=False)
    )
    elements.append(
        textbox_xml(3, "Title", emu(0.8), emu(0.09), emu(7.0), emu(0.45), [paragraph_xml(title, size=2600, color="FFFFFF", bold=True)], inset=(0, 0, 0, 0))
    )
    x0 = emu(0.8)
    step_w = emu(2.55)
    gap = emu(0.32)
    y = emu(2.0)
    for idx, (step_title, desc) in enumerate(steps):
        x = x0 + idx * (step_w + gap)
        sid = 4 + idx * 3
        elements.append(
            textbox_xml(
                sid,
                f"StepNo{idx}",
                x + emu(0.9),
                emu(1.2),
                emu(0.7),
                emu(0.7),
                [paragraph_xml(str(idx + 1), size=2400, color="FFFFFF", bold=True, align="ctr")],
                fill=rgb_fill("1F5EAB"),
                line=line_fill(None),
                inset=(0, 0, 0, 0),
            )
        )
        elements.append(
            textbox_xml(
                sid + 1,
                f"StepBox{idx}",
                x,
                y,
                step_w,
                emu(2.3),
                [
                    paragraph_xml(step_title, size=1900, color="1F5EAB", bold=True, align="ctr"),
                    paragraph_xml(""),
                    paragraph_xml(desc, size=1450, color="333333", align="ctr"),
                ],
                fill=rgb_fill("FFFFFF"),
                line=line_fill("D7E3F3", width=19050),
                inset=(emu(0.15), emu(0.15), emu(0.15), emu(0.15)),
                anchor="ctr",
            )
        )
        if idx < len(steps) - 1:
            elements.append(
                textbox_xml(
                    sid + 2,
                    f"Connector{idx}",
                    x + step_w,
                    emu(2.28),
                    gap,
                    emu(0.08),
                    [],
                    fill=rgb_fill("76D0AB"),
                    line=line_fill(None),
                    tx_box=False,
                )
            )
    elements.extend(footer_elements(30, page_label))
    return slide_xml(elements)


def software_main_flow_slide(page_label: str) -> str:
    elements: list[str] = []
    elements.append(
        textbox_xml(2, "Background", 0, 0, SLIDE_W, SLIDE_H, [], fill=rgb_fill("F7FAFE"), line=line_fill(None), tx_box=False)
    )
    elements.append(
        textbox_xml(
            3,
            "Title",
            emu(0.8),
            emu(0.8),
            emu(4.8),
            emu(0.7),
            [paragraph_xml("系统主流程图", size=2850, color="163B66", bold=True)],
            inset=(0, 0, 0, 0),
        )
    )
    elements.append(
        textbox_xml(
            4,
            "Subtitle",
            emu(0.82),
            emu(1.45),
            emu(5.2),
            emu(0.5),
            [paragraph_xml("把启动、配网、联网、采集和上传串起来之后，程序主线会更好讲", size=1350, color="5E7184")],
            inset=(0, 0, 0, 0),
        )
    )
    elements.append(
        textbox_xml(5, "LeftPanel", emu(0.72), emu(2.0), emu(2.55), emu(4.2), [], fill=rgb_fill("173E69"), line=line_fill(None), tx_box=False)
    )
    elements.append(
        textbox_xml(
            6,
            "LeftPanelText",
            emu(0.95),
            emu(2.35),
            emu(2.05),
            emu(3.2),
            [
                paragraph_xml("主线提示", size=2200, color="FFFFFF", bold=True, align="ctr"),
                paragraph_xml(""),
                paragraph_xml("1. 先把设备和参数带起来", size=1500, color="D7E8F9"),
                paragraph_xml("2. 再把联网链路跑通", size=1500, color="D7E8F9"),
                paragraph_xml("3. 最后统一采集并上传", size=1500, color="D7E8F9"),
            ],
            inset=(0, 0, 0, 0),
            anchor="ctr",
        )
    )

    main_steps = [
        ("系统上电", "进入 app_main"),
        ("基础初始化", "NVS / GPIO / LoRa / 传感器"),
        ("读取历史 WiFi", "尝试加载已保存配置"),
        ("连接 WiFi", "拿到 IP 后进入主链路"),
        ("启动 MQTT", "建立平台通信"),
        ("采集与轮询", "空气、土壤、LoRa 节点"),
        ("统一上传", "JSON 打包 + NVS 状态保存"),
    ]
    x = emu(3.65)
    w = emu(3.15)
    y_positions = [emu(1.8), emu(2.5), emu(3.2), emu(4.15), emu(4.85), emu(5.55), emu(6.25)]
    for idx, ((title, desc), y) in enumerate(zip(main_steps, y_positions), start=10):
        elements.append(
            textbox_xml(
                idx,
                f"MainStep{idx}",
                x,
                y,
                w,
                emu(0.52),
                [
                    paragraph_xml(title, size=1700, color="1F5EAB", bold=True, align="ctr"),
                    paragraph_xml(desc, size=1080, color="516679", align="ctr"),
                ],
                fill=rgb_fill("FFFFFF"),
                line=line_fill("BFD0E1", width=19050),
                inset=(emu(0.1), emu(0.05), emu(0.1), emu(0.04)),
                anchor="ctr",
            )
        )
        if idx < 16:
            elements.append(
                textbox_xml(
                    idx + 20,
                    f"MainArrow{idx}",
                    x + emu(1.26),
                    y + emu(0.53),
                    emu(0.62),
                    emu(0.18),
                    [paragraph_xml("↓", size=1750, color="76D0AB", bold=True, align="ctr")],
                    inset=(0, 0, 0, 0),
                )
            )

    elements.append(
        textbox_xml(
            40,
            "Decision",
            emu(7.4),
            emu(3.16),
            emu(1.75),
            emu(0.56),
            [
                paragraph_xml("无可用 WiFi？", size=1500, color="C8872D", bold=True, align="ctr"),
            ],
            fill=rgb_fill("FFF5E8"),
            line=line_fill("C8872D", width=19050),
            inset=(emu(0.08), emu(0.04), emu(0.08), emu(0.04)),
            anchor="ctr",
        )
    )
    branch_steps = [
        ("启动 AP 配网", emu(7.4), emu(4.0), "1F5EAB", "EAF3FF"),
        ("网页提交信息", emu(9.42), emu(4.0), "38A37A", "EFFAF5"),
        ("保存新凭据", emu(8.42), emu(4.88), "C8872D", "FFF5E8"),
    ]
    for sid, (text, bx, by, accent, fill_color) in enumerate(branch_steps, start=41):
        elements.append(
            textbox_xml(
                sid,
                f"Branch{sid}",
                bx,
                by,
                emu(1.72),
                emu(0.56),
                [paragraph_xml(text, size=1450, color=accent, bold=True, align="ctr")],
                fill=rgb_fill(fill_color),
                line=line_fill(accent, width=19050),
                inset=(emu(0.06), emu(0.04), emu(0.06), emu(0.04)),
                anchor="ctr",
            )
        )

    arrow_specs = [
        (50, emu(6.95), emu(3.28), emu(0.42), emu(0.18), "→", "C8872D"),
        (51, emu(8.95), emu(4.14), emu(0.38), emu(0.18), "→", "38A37A"),
        (52, emu(9.05), emu(4.55), emu(0.18), emu(0.26), "↓", "C8872D"),
        (53, emu(7.95), emu(5.02), emu(0.32), emu(0.18), "←", "C8872D"),
        (54, emu(7.15), emu(4.22), emu(0.18), emu(0.58), "↑", "1F5EAB"),
    ]
    for sid, ax, ay, acx, acy, mark, color in arrow_specs:
        elements.append(
            textbox_xml(
                sid,
                f"Arrow{sid}",
                ax,
                ay,
                acx,
                acy,
                [paragraph_xml(mark, size=1700, color=color, bold=True, align="ctr")],
                inset=(0, 0, 0, 0),
            )
        )

    elements.append(
        textbox_xml(
            60,
            "Note",
            emu(7.35),
            emu(5.88),
            emu(3.72),
            emu(0.72),
            [paragraph_xml("如果老师问配网逻辑，这里就可以顺着讲成：先读历史配置，不行就进 AP，再回到主链路", size=1280, color="4B6278")],
            fill=rgb_fill("EEF6FC"),
            line=line_fill("BFD0E1", width=12700),
            inset=(emu(0.12), emu(0.08), emu(0.12), emu(0.06)),
            anchor="ctr",
        )
    )
    elements.extend(footer_elements(70, page_label))
    return slide_xml(elements)


def ap_provisioning_flow_slide(page_label: str) -> str:
    elements: list[str] = []
    elements.append(
        textbox_xml(2, "Background", 0, 0, SLIDE_W, SLIDE_H, [], fill=rgb_fill("F7FAFE"), line=line_fill(None), tx_box=False)
    )
    elements.append(
        textbox_xml(
            3,
            "Title",
            emu(0.8),
            emu(0.8),
            emu(4.8),
            emu(0.7),
            [paragraph_xml("AP 配网流程图", size=2850, color="163B66", bold=True)],
            inset=(0, 0, 0, 0),
        )
    )
    elements.append(
        textbox_xml(
            4,
            "Subtitle",
            emu(0.82),
            emu(1.45),
            emu(6.4),
            emu(0.46),
            [paragraph_xml("这一页专门说明设备第一次怎么联网，为什么现场部署时不用额外 App 也能完成配置", size=1320, color="5E7184")],
            inset=(0, 0, 0, 0),
        )
    )
    elements.append(
        textbox_xml(5, "LeftPanel", emu(0.72), emu(2.05), emu(2.8), emu(4.15), [], fill=rgb_fill("173E69"), line=line_fill(None), tx_box=False)
    )
    elements.append(
        textbox_xml(
            6,
            "LeftPanelText",
            emu(0.95),
            emu(2.32),
            emu(2.28),
            emu(3.45),
            [
                paragraph_xml("讲解重点", size=2150, color="FFFFFF", bold=True, align="ctr"),
                paragraph_xml(""),
                paragraph_xml("1. 没有历史 WiFi 时，设备会自动开热点", size=1450, color="D7E8F9"),
                paragraph_xml("2. 用户通过浏览器提交路由器账号密码", size=1450, color="D7E8F9"),
                paragraph_xml("3. 保存成功后，系统自动回到主链路联网", size=1450, color="D7E8F9"),
                paragraph_xml("4. 如果 WiFi 长时间失败，再启动 Cat1 备份", size=1450, color="D7E8F9"),
            ],
            inset=(0, 0, 0, 0),
            anchor="ctr",
        )
    )

    steps = [
        ("检查历史 WiFi", "读取已保存 SSID / Password"),
        ("开启 AP 热点", "广播 ESP32_Config"),
        ("连接热点", "手机或电脑接入 AP"),
        ("网页提交信息", "输入路由器名称和密码"),
        ("保存并重连", "写入 NVS 后尝试 STA 联网"),
        ("获取 IP", "成功后启动 MQTT 主链路"),
    ]
    x = emu(4.1)
    w = emu(3.0)
    y_positions = [emu(1.95), emu(2.7), emu(3.45), emu(4.2), emu(4.95), emu(5.7)]
    for idx, ((title, desc), y) in enumerate(zip(steps, y_positions), start=10):
        elements.append(
            textbox_xml(
                idx,
                f"ApStep{idx}",
                x,
                y,
                w,
                emu(0.56),
                [
                    paragraph_xml(title, size=1700, color="1F5EAB", bold=True, align="ctr"),
                    paragraph_xml(desc, size=1060, color="556A7D", align="ctr"),
                ],
                fill=rgb_fill("FFFFFF"),
                line=line_fill("BFD0E1", width=19050),
                inset=(emu(0.08), emu(0.05), emu(0.08), emu(0.04)),
                anchor="ctr",
            )
        )
        if idx < 15:
            elements.append(
                textbox_xml(
                    idx + 20,
                    f"ApArrow{idx}",
                    x + emu(1.18),
                    y + emu(0.56),
                    emu(0.64),
                    emu(0.18),
                    [paragraph_xml("↓", size=1750, color="76D0AB", bold=True, align="ctr")],
                    inset=(0, 0, 0, 0),
                )
            )

    elements.append(
        textbox_xml(
            40,
            "ScreenTitle",
            emu(7.78),
            emu(1.95),
            emu(2.7),
            emu(0.34),
            [paragraph_xml("AP 配网页面实拍", size=1850, color="38A37A", bold=True, align="ctr")],
            inset=(0, 0, 0, 0),
        )
    )
    elements.append(
        textbox_xml(
            41,
            "ScreenCard",
            emu(7.72),
            emu(2.3),
            emu(2.65),
            emu(3.95),
            [],
            fill=rgb_fill("FFFFFF"),
            line=line_fill("76D0AB", width=19050),
            tx_box=False,
        )
    )
    elements.append(picture_xml(42, emu(8.05), emu(2.48), emu(1.56), emu(3.47), "rId2"))
    elements.append(
        textbox_xml(
            43,
            "ScreenNote",
            emu(9.82),
            emu(2.4),
            emu(1.92),
            emu(2.15),
            [
                paragraph_xml("实拍说明", size=1600, color="C8872D", bold=True),
                paragraph_xml(""),
                paragraph_xml("- 页面会展示附近 2.4GHz WiFi", size=1120, color="394D5E"),
                paragraph_xml("- 用户也可以手动输入 SSID", size=1120, color="394D5E"),
                paragraph_xml("- 提交后写入 NVS 并自动重连", size=1120, color="394D5E"),
            ],
            fill=rgb_fill("FFF5E8"),
            line=line_fill("C8872D", width=19050),
            inset=(emu(0.1), emu(0.1), emu(0.1), emu(0.08)),
            anchor="t",
        )
    )
    elements.append(
        textbox_xml(
            44,
            "FallbackBox",
            emu(9.82),
            emu(4.9),
            emu(1.92),
            emu(1.05),
            [
                paragraph_xml("如果 WiFi 长时间失败，程序不会一直卡在这里，而是会继续考虑 Cat1 备份链路", size=1080, color="6B5945"),
            ],
            fill=rgb_fill("F4F8FC"),
            line=line_fill("BFD0E1", width=12700),
            inset=(emu(0.08), emu(0.08), emu(0.08), emu(0.06)),
            anchor="ctr",
        )
    )
    elements.extend(footer_elements(50, page_label))
    return slide_xml(elements)


def data_upload_flow_slide(page_label: str) -> str:
    elements: list[str] = []
    elements.append(
        textbox_xml(2, "Background", 0, 0, SLIDE_W, SLIDE_H, [], fill=rgb_fill("FCFDFC"), line=line_fill(None), tx_box=False)
    )
    elements.append(
        textbox_xml(
            3,
            "Title",
            emu(0.8),
            emu(0.82),
            emu(5.8),
            emu(0.72),
            [paragraph_xml("数据上传流程图", size=2850, color="173E69", bold=True)],
            inset=(0, 0, 0, 0),
        )
    )
    elements.append(
        textbox_xml(
            4,
            "Desc",
            emu(0.82),
            emu(1.45),
            emu(6.2),
            emu(0.46),
            [paragraph_xml("这一页最适合说明网关为什么不是简单中转，而是做了汇总、解析和统一上报", size=1330, color="5B6E80")],
            inset=(0, 0, 0, 0),
        )
    )

    source_boxes = [
        ("土壤参数", "温湿度 / 电导率 / 氮磷钾", emu(0.92), emu(2.15), "1F5EAB", "EAF3FF"),
        ("空气参数", "SHT30 + BH1750 真实采集", emu(0.92), emu(3.48), "38A37A", "EFFAF5"),
        ("LoRa 节点", "子节点环境数据回传", emu(0.92), emu(4.81), "C8872D", "FFF5E8"),
    ]
    for sid, (title, desc, x, y, accent, fill_color) in enumerate(source_boxes, start=10):
        elements.append(
            textbox_xml(
                sid,
                f"Source{sid}",
                x,
                y,
                emu(2.55),
                emu(0.9),
                [
                    paragraph_xml(title, size=1800, color=accent, bold=True, align="ctr"),
                    paragraph_xml(desc, size=1150, color="4D6173", align="ctr"),
                ],
                fill=rgb_fill(fill_color),
                line=line_fill(accent, width=19050),
                inset=(emu(0.08), emu(0.08), emu(0.08), emu(0.06)),
                anchor="ctr",
            )
        )
        elements.append(
            textbox_xml(
                sid + 10,
                f"SourceArrow{sid}",
                emu(3.63),
                y + emu(0.28),
                emu(0.55),
                emu(0.2),
                [paragraph_xml("→", size=1700, color=accent, bold=True, align="ctr")],
                inset=(0, 0, 0, 0),
            )
        )

    elements.append(
        textbox_xml(
            30,
            "GatewayBox",
            emu(4.32),
            emu(2.58),
            emu(2.6),
            emu(2.55),
            [
                paragraph_xml("网关统一处理", size=2200, color="173E69", bold=True, align="ctr"),
                paragraph_xml(""),
                paragraph_xml("数据汇总", size=1600, color="1F5EAB", bold=True, align="ctr"),
                paragraph_xml("字段映射", size=1600, color="1F5EAB", bold=True, align="ctr"),
                paragraph_xml("异常过滤", size=1600, color="1F5EAB", bold=True, align="ctr"),
            ],
            fill=rgb_fill("FFFFFF"),
            line=line_fill("173E69", width=25400),
            inset=(emu(0.12), emu(0.12), emu(0.12), emu(0.1)),
            anchor="ctr",
        )
    )

    right_steps = [
        ("JSON 封装", "统一生成平台上报载荷", emu(7.35), emu(2.15), "1F5EAB", "EAF3FF"),
        ("MQTT 发布", "通过主题上报到 OneNET", emu(7.35), emu(3.45), "38A37A", "EFFAF5"),
        ("平台展示", "状态显示、远程查看与管理", emu(7.35), emu(4.75), "C8872D", "FFF5E8"),
    ]
    for sid, (title, desc, x, y, accent, fill_color) in enumerate(right_steps, start=31):
        elements.append(
            textbox_xml(
                sid,
                f"RightStep{sid}",
                x,
                y,
                emu(3.45),
                emu(0.92),
                [
                    paragraph_xml(title, size=1800, color=accent, bold=True, align="ctr"),
                    paragraph_xml(desc, size=1180, color="4D6173", align="ctr"),
                ],
                fill=rgb_fill(fill_color),
                line=line_fill(accent, width=19050),
                inset=(emu(0.08), emu(0.08), emu(0.08), emu(0.06)),
                anchor="ctr",
            )
        )
    arrow_specs = [
        (40, emu(6.98), emu(3.62), emu(0.3), emu(0.2), "→", "173E69"),
        (41, emu(8.8), emu(3.1), emu(0.22), emu(0.24), "↓", "38A37A"),
        (42, emu(8.8), emu(4.4), emu(0.22), emu(0.24), "↓", "C8872D"),
    ]
    for sid, ax, ay, acx, acy, mark, color in arrow_specs:
        elements.append(
            textbox_xml(
                sid,
                f"Arrow{sid}",
                ax,
                ay,
                acx,
                acy,
                [paragraph_xml(mark, size=1700, color=color, bold=True, align="ctr")],
                inset=(0, 0, 0, 0),
            )
        )

    elements.append(
        textbox_xml(
            50,
            "Note",
            emu(0.92),
            emu(6.15),
            emu(9.85),
            emu(0.55),
            [paragraph_xml("答辩时可以顺手强调一句：多路数据最后没有各传各的，而是先在网关侧做统一组织，再走同一条平台上传链路", size=1280, color="4B6278")],
            fill=rgb_fill("F4F8FC"),
            line=line_fill("C6D6E6", width=12700),
            inset=(emu(0.12), emu(0.08), emu(0.12), emu(0.06)),
            anchor="ctr",
        )
    )
    elements.extend(footer_elements(60, page_label))
    return slide_xml(elements)


def ota_upgrade_flow_slide(page_label: str) -> str:
    elements: list[str] = []
    elements.append(
        textbox_xml(2, "Background", 0, 0, SLIDE_W, SLIDE_H, [], fill=rgb_fill("FFF9F1"), line=line_fill(None), tx_box=False)
    )
    elements.append(
        textbox_xml(
            3,
            "Title",
            emu(0.8),
            emu(0.82),
            emu(5.5),
            emu(0.72),
            [paragraph_xml("OTA 升级流程图", size=2850, color="8A5318", bold=True)],
            inset=(0, 0, 0, 0),
        )
    )
    elements.append(
        textbox_xml(
            4,
            "Desc",
            emu(0.82),
            emu(1.45),
            emu(6.5),
            emu(0.46),
            [paragraph_xml("这页用来讲清楚“升级包进来以后系统内部到底怎么走”", size=1330, color="7A664F")],
            inset=(0, 0, 0, 0),
        )
    )
    elements.append(
        textbox_xml(5, "MidLine", emu(0.95), emu(3.52), emu(10.35), emu(0.08), [], fill=rgb_fill("D8B48A"), line=line_fill(None), tx_box=False)
    )

    steps = [
        ("01", "版本上报", "设备先上报当前版本", emu(0.95), emu(2.0), "1F5EAB", "EAF3FF"),
        ("02", "任务检测", "平台判断是否有升级", emu(2.45), emu(4.05), "38A37A", "EFFAF5"),
        ("03", "下载固件", "HTTP/HTTPS 获取升级包", emu(3.95), emu(2.0), "C8872D", "FFF5E8"),
        ("04", "分片缓存", "进入队列等待写入", emu(5.45), emu(4.05), "4E6FAE", "F4F6FE"),
        ("05", "写入分区", "写入 ota_0 / ota_1", emu(6.95), emu(2.0), "1F5EAB", "EAF3FF"),
        ("06", "校验切换", "校验成功后切换启动分区", emu(8.45), emu(4.05), "38A37A", "EFFAF5"),
        ("07", "重启回报", "运行新版本并回报结果", emu(9.95), emu(2.0), "C8872D", "FFF5E8"),
    ]
    for sid, (num, title, desc, x, y, accent, fill_color) in enumerate(steps, start=10):
        elements.append(
            textbox_xml(
                sid,
                f"StepCard{sid}",
                x,
                y,
                emu(1.2),
                emu(1.0),
                [
                    paragraph_xml(num, size=1500, color=accent, bold=True, align="ctr"),
                    paragraph_xml(title, size=1550, color=accent, bold=True, align="ctr"),
                    paragraph_xml(desc, size=980, color="5B6670", align="ctr"),
                ],
                fill=rgb_fill(fill_color),
                line=line_fill(accent, width=19050),
                inset=(emu(0.05), emu(0.05), emu(0.05), emu(0.05)),
                anchor="ctr",
            )
        )
        elements.append(
            textbox_xml(
                sid + 20,
                f"StepAnchor{sid}",
                x + emu(0.49),
                emu(3.3),
                emu(0.22),
                emu(0.22),
                [],
                fill=rgb_fill(accent),
                line=line_fill(None),
                tx_box=False,
            )
        )
    arrow_marks = [
        (40, emu(2.0), emu(3.3), emu(0.28), emu(0.2), "→", "38A37A"),
        (41, emu(3.5), emu(3.3), emu(0.28), emu(0.2), "→", "C8872D"),
        (42, emu(5.0), emu(3.3), emu(0.28), emu(0.2), "→", "4E6FAE"),
        (43, emu(6.5), emu(3.3), emu(0.28), emu(0.2), "→", "1F5EAB"),
        (44, emu(8.0), emu(3.3), emu(0.28), emu(0.2), "→", "38A37A"),
        (45, emu(9.5), emu(3.3), emu(0.28), emu(0.2), "→", "C8872D"),
    ]
    for sid, ax, ay, acx, acy, mark, color in arrow_marks:
        elements.append(
            textbox_xml(
                sid,
                f"TimelineArrow{sid}",
                ax,
                ay,
                acx,
                acy,
                [paragraph_xml(mark, size=1700, color=color, bold=True, align="ctr")],
                inset=(0, 0, 0, 0),
            )
        )

    elements.append(
        textbox_xml(
            60,
            "Note",
            emu(0.95),
            emu(5.85),
            emu(10.5),
            emu(0.65),
            [paragraph_xml("这里可以补一句当前实现状态：网关本体 OTA 主流程已经打通，LoRa 子节点 OTA 还在继续完善，所以现阶段更适合表述为“基础升级链路已具备”", size=1270, color="6A5B49")],
            fill=rgb_fill("FFF1DF"),
            line=line_fill("D8B48A", width=12700),
            inset=(emu(0.12), emu(0.08), emu(0.12), emu(0.06)),
            anchor="ctr",
        )
    )
    elements.extend(footer_elements(70, page_label))
    return slide_xml(elements)


def test_dashboard_slide(page_label: str) -> str:
    elements: list[str] = []
    elements.append(
        textbox_xml(2, "TopBand", 0, 0, SLIDE_W, emu(0.72), [], fill=rgb_fill("1F5EAB"), line=line_fill(None), tx_box=False)
    )
    elements.append(
        textbox_xml(3, "Title", emu(0.8), emu(0.09), emu(7.0), emu(0.45), [paragraph_xml("测试结果总览", size=2600, color="FFFFFF", bold=True)], inset=(0, 0, 0, 0))
    )
    metrics = [
        ("基础功能", "7 / 7 测试项启动正常", "1F5EAB"),
        ("通信功能", "7 / 7 链路验证通过", "38A37A"),
        ("平台上报", "6 / 6 数据项显示正常", "4E6FAE"),
    ]
    for idx, (label, desc, color) in enumerate(metrics):
        x = emu(0.8 + idx * 3.95)
        sid = 4 + idx
        elements.append(
            textbox_xml(
                sid,
                f"Metric{idx}",
                x,
                emu(1.25),
                emu(3.35),
                emu(1.55),
                [
                    paragraph_xml(label, size=2100, color=color, bold=True, align="ctr"),
                    paragraph_xml(desc, size=1350, color="36495E", align="ctr"),
                ],
                fill=rgb_fill("FFFFFF"),
                line=line_fill(color, width=25400),
                inset=(emu(0.14), emu(0.14), emu(0.14), emu(0.14)),
                anchor="ctr",
            )
        )
    elements.append(
        textbox_xml(
            10,
            "SummaryBox",
            emu(0.8),
            emu(3.1),
            emu(7.0),
            emu(2.9),
            [
                paragraph_xml("结果解读", size=2200, color="1F5EAB", bold=True),
                paragraph_xml(""),
                paragraph_xml("- 三类数据源已经进入统一链路，包括网关空气、土壤传感器和 LoRa 子节点数据", size=1600),
                paragraph_xml("- 平台侧可正常解析温度、湿度、光照、EC 以及氮磷钾等关键字段", size=1600),
                paragraph_xml("- OTA 方面可概括为网关本体基本实现，LoRa 子节点远程升级仍待完善", size=1600),
            ],
            fill=rgb_fill("F7FAFE"),
            line=line_fill("D7E3F3", width=19050),
            inset=(emu(0.18), emu(0.18), emu(0.18), emu(0.14)),
            anchor="t",
        )
    )
    elements.append(
        textbox_xml(
            11,
            "TagsBox",
            emu(8.15),
            emu(3.1),
            emu(3.05),
            emu(2.9),
            [
                paragraph_xml("实验指标", size=2200, color="38A37A", bold=True, align="ctr"),
                paragraph_xml(""),
                paragraph_xml("3 类数据源", size=1750, color="1F5EAB", bold=True, align="ctr"),
                paragraph_xml("2 条联网链路", size=1750, color="1F5EAB", bold=True, align="ctr"),
                paragraph_xml("6 类平台显示项", size=1750, color="1F5EAB", bold=True, align="ctr"),
                paragraph_xml("网关 OTA 已接入", size=1750, color="1F5EAB", bold=True, align="ctr"),
            ],
            fill=rgb_fill("EFFAF5"),
            line=line_fill("76D0AB", width=19050),
            inset=(emu(0.15), emu(0.15), emu(0.15), emu(0.12)),
            anchor="ctr",
        )
    )
    elements.extend(footer_elements(20, page_label))
    return slide_xml(elements)


def summary_slide(page_label: str) -> str:
    return band_story_slide(
        "总结与展望",
        [
            ("完成内容", "完成了基于 ESP32 的农业智能助手原型设计，实现多源采集、LoRa 接入、WiFi/Cat1 联网和 MQTT 平台交互", "1F5EAB"),
            ("实验结论", "基础功能、通信链路、平台上报和网关 OTA 主流程均完成验证，系统已达到课题阶段性目标", "38A37A"),
            ("后续优化", "继续完善 LoRa 子节点 OTA、多轮升级容错、长期运行稳定性以及硬件工程化集成能力", "C8872D"),
        ],
        page_label,
    )


def architecture_slide(page_label: str) -> str:
    elements: list[str] = []
    elements.append(
        textbox_xml(2, "TopBand", 0, 0, SLIDE_W, emu(0.72), [], fill=rgb_fill("1F5EAB"), line=line_fill(None), tx_box=False)
    )
    elements.append(
        textbox_xml(3, "Title", emu(0.8), emu(0.09), emu(7.2), emu(0.45), [paragraph_xml("系统总体架构", size=2600, color="FFFFFF", bold=True)], inset=(0, 0, 0, 0))
    )
    boxes = [
        ("感知层", "土壤传感器\n网关空气传感器\nLoRa 子节点", "EAF4FF", "1F5EAB", emu(0.9)),
        ("网关层", "ESP32 主控\nWiFi / Cat1\nLoRa / MQTT / NVS\nOTA 维护", "EFFAF5", "38A37A", emu(4.55)),
        ("平台层", "OneNET 云平台\n数据展示\n远程监控\n状态交互", "F5F7FB", "5A6A85", emu(8.2)),
    ]
    sid = 4
    for title, content, fill_color, line_color, x in boxes:
        elements.append(
            textbox_xml(
                sid,
                f"Box{sid}",
                x,
                emu(2.0),
                emu(3.0),
                emu(2.5),
                [
                    paragraph_xml(title, size=2200, color=line_color, bold=True, align="ctr"),
                    paragraph_xml(""),
                    *[paragraph_xml(line, size=1700, color="2C2C2C", align="ctr") for line in content.split("\n")],
                ],
                fill=rgb_fill(fill_color),
                line=line_fill(line_color, width=19050),
                inset=(emu(0.15), emu(0.18), emu(0.15), emu(0.15)),
                anchor="ctr",
            )
        )
        sid += 1
    elements.append(
        textbox_xml(
            sid,
            "FlowText",
            emu(1.35),
            emu(4.95),
            emu(10.3),
            emu(0.5),
            [paragraph_xml("现场采集  →  网关汇聚与调度  →  平台展示与管理", size=2000, color="1F5EAB", bold=True, align="ctr")],
            inset=(0, 0, 0, 0),
        )
    )
    elements.extend(footer_elements(sid + 1, page_label))
    return slide_xml(elements)


def cover_slide() -> str:
    elements: list[str] = []
    elements.append(picture_xml(2, 0, 0, SLIDE_W, SLIDE_H, "rId2"))
    elements.append(
        textbox_xml(
            3,
            "WhiteWash",
            0,
            0,
            SLIDE_W,
            SLIDE_H,
            [],
            fill=rgb_fill("FFFFFF", alpha=70000),
            line=line_fill(None),
            tx_box=False,
        )
    )
    shape_id = 10
    y = 0.18
    while y < 6.55:
        elements.append(
            textbox_xml(
                shape_id,
                f"Line{shape_id}",
                0,
                emu(y),
                SLIDE_W,
                emu(0.015),
                [],
                fill=rgb_fill("D8E2F0"),
                line=line_fill(None),
                tx_box=False,
            )
        )
        shape_id += 1
        y += 0.28

    elements.append(
        textbox_xml(
            100,
            "SchoolBand",
            emu(0.12),
            emu(0.18),
            emu(3.25),
            emu(0.58),
            [],
            fill=rgb_fill("FFFFFF", alpha=90000),
            line=line_fill(None),
            tx_box=False,
        )
    )
    elements.append(
        textbox_xml(
            101,
            "SchoolAccent",
            emu(0.2),
            emu(0.28),
            emu(0.04),
            emu(0.38),
            [],
            fill=rgb_fill("72CBA8"),
            line=line_fill(None),
            tx_box=False,
        )
    )
    elements.append(
        textbox_xml(
            102,
            "SchoolName",
            emu(0.36),
            emu(0.23),
            emu(2.8),
            emu(0.42),
            [
                paragraph_xml("广东海洋大学", size=1800, color="0E4A94", bold=True),
                paragraph_xml("GUANGDONG OCEAN UNIVERSITY", size=860, color="0E4A94", bold=True),
            ],
            inset=(0, 0, 0, 0),
        )
    )
    elements.append(
        textbox_xml(
            103,
            "Motto",
            emu(10.55),
            emu(0.2),
            emu(1.9),
            emu(0.22),
            [paragraph_xml("明德博学  笃行合一——", size=900, color="0E4A94", bold=True, align="r")],
            inset=(0, 0, 0, 0),
        )
    )
    elements.append(
        textbox_xml(
            104,
            "Title",
            emu(2.4),
            emu(1.4),
            emu(7.2),
            emu(1.2),
            [
                paragraph_xml("基于ESP32的农业智能助手", size=2550, color="114B99", bold=True, align="ctr"),
                paragraph_xml("设计与实现", size=2550, color="114B99", bold=True, align="ctr"),
            ],
            inset=(0, 0, 0, 0),
            anchor="ctr",
        )
    )
    elements.append(
        textbox_xml(
            105,
            "Divider",
            emu(1.7),
            emu(3.0),
            emu(8.7),
            emu(0.02),
            [],
            fill=rgb_fill("D8E6F8"),
            line=line_fill(None),
            tx_box=False,
        )
    )
    elements.append(
        textbox_xml(
            106,
            "EnglishTitle",
            emu(1.9),
            emu(3.22),
            emu(8.25),
            emu(0.72),
            [
                paragraph_xml("DESIGN AND IMPLEMENTATION OF AN AGRICULTURAL", size=1300, color="114B99", bold=True, align="ctr", font="Times New Roman"),
                paragraph_xml("INTELLIGENT ASSISTANT BASED ON ESP32", size=1300, color="114B99", bold=True, align="ctr", font="Times New Roman"),
            ],
            inset=(0, 0, 0, 0),
            anchor="ctr",
        )
    )
    elements.append(
        textbox_xml(
            107,
            "InfoBand",
            emu(0.8),
            emu(5.05),
            emu(10.7),
            emu(0.48),
            [],
            fill=rgb_fill("FFFFFF", alpha=76000),
            line=line_fill(None),
            tx_box=False,
        )
    )
    info_specs = [
        (108, emu(0.95), emu(5.08), emu(2.1), "汇报人：邓晓岚"),
        (109, emu(3.15), emu(5.08), emu(2.0), "专业：通信工程"),
        (110, emu(5.35), emu(5.08), emu(2.2), "指导教师：李晓玉"),
        (111, emu(7.95), emu(5.08), emu(3.15), "答辩时间：2026年5月24日"),
    ]
    for sid, x, y_pos, width, text in info_specs:
        elements.append(
            textbox_xml(
                sid,
                f"Info{sid}",
                x,
                y_pos,
                width,
                emu(0.22),
                [paragraph_xml(text, size=1360, color="114B99", bold=True, align="ctr")],
                inset=(0, 0, 0, 0),
            )
        )
    return slide_xml(elements)


def thanks_slide() -> str:
    elements: list[str] = []
    elements.append(picture_xml(2, 0, 0, SLIDE_W, SLIDE_H, "rId2"))
    elements.append(
        textbox_xml(
            3,
            "Overlay",
            emu(3.25),
            emu(1.8),
            emu(6.0),
            emu(2.1),
            [],
            fill=rgb_fill("FFFFFF", alpha=70000),
            line=line_fill(None),
            tx_box=False,
        )
    )
    elements.append(
        textbox_xml(
            4,
            "Thanks",
            emu(3.45),
            emu(2.15),
            emu(5.6),
            emu(1.45),
            [
                paragraph_xml("感谢聆听", size=3000, color="1B4F8F", bold=True, align="ctr"),
                paragraph_xml("敬请各位老师批评指正", size=1500, color="4F617A", align="ctr"),
            ],
            inset=(0, 0, 0, 0),
            anchor="ctr",
        )
    )
    return slide_xml(elements)


def slide_rels(with_image: bool = False, media_rels: list[tuple[str, str]] | None = None) -> str:
    rels = [
        (
            "rId1",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideLayout",
            "../slideLayouts/slideLayout1.xml",
        )
    ]
    if with_image:
        rels.append(
            (
                "rId2",
                "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image",
                "../media/image1.png",
            )
        )
    if media_rels:
        for rid, target in media_rels:
            rels.append(
                (
                    rid,
                    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image",
                    target,
                )
            )
    rel_xml = "".join(
        f"<Relationship Id=\"{rid}\" Type=\"{rtype}\" Target=\"{target}\"/>"
        for rid, rtype, target in rels
    )
    return (
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        f"{rel_xml}</Relationships>"
    )


THEME_XML = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<a:theme xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" name="Office Theme">
  <a:themeElements>
    <a:clrScheme name="Office">
      <a:dk1><a:sysClr val="windowText" lastClr="000000"/></a:dk1>
      <a:lt1><a:sysClr val="window" lastClr="FFFFFF"/></a:lt1>
      <a:dk2><a:srgbClr val="1F1F1F"/></a:dk2>
      <a:lt2><a:srgbClr val="EEECE1"/></a:lt2>
      <a:accent1><a:srgbClr val="4F81BD"/></a:accent1>
      <a:accent2><a:srgbClr val="C0504D"/></a:accent2>
      <a:accent3><a:srgbClr val="9BBB59"/></a:accent3>
      <a:accent4><a:srgbClr val="8064A2"/></a:accent4>
      <a:accent5><a:srgbClr val="4BACC6"/></a:accent5>
      <a:accent6><a:srgbClr val="F79646"/></a:accent6>
      <a:hlink><a:srgbClr val="0000FF"/></a:hlink>
      <a:folHlink><a:srgbClr val="800080"/></a:folHlink>
    </a:clrScheme>
    <a:fontScheme name="Office">
      <a:majorFont><a:latin typeface="Microsoft YaHei"/><a:ea typeface="Microsoft YaHei"/><a:cs typeface="Microsoft YaHei"/></a:majorFont>
      <a:minorFont><a:latin typeface="Microsoft YaHei"/><a:ea typeface="Microsoft YaHei"/><a:cs typeface="Microsoft YaHei"/></a:minorFont>
    </a:fontScheme>
    <a:fmtScheme name="Office">
      <a:fillStyleLst>
        <a:solidFill><a:schemeClr val="phClr"/></a:solidFill>
        <a:gradFill rotWithShape="1">
          <a:gsLst>
            <a:gs pos="0"><a:schemeClr val="phClr"><a:tint val="50000"/><a:satMod val="300000"/></a:schemeClr></a:gs>
            <a:gs pos="35000"><a:schemeClr val="phClr"><a:tint val="37000"/><a:satMod val="300000"/></a:schemeClr></a:gs>
            <a:gs pos="100000"><a:schemeClr val="phClr"><a:tint val="15000"/><a:satMod val="350000"/></a:schemeClr></a:gs>
          </a:gsLst>
          <a:lin ang="16200000" scaled="1"/>
        </a:gradFill>
        <a:gradFill rotWithShape="1">
          <a:gsLst>
            <a:gs pos="0"><a:schemeClr val="phClr"><a:shade val="51000"/><a:satMod val="130000"/></a:schemeClr></a:gs>
            <a:gs pos="80000"><a:schemeClr val="phClr"><a:shade val="93000"/><a:satMod val="130000"/></a:schemeClr></a:gs>
            <a:gs pos="100000"><a:schemeClr val="phClr"><a:shade val="94000"/><a:satMod val="135000"/></a:schemeClr></a:gs>
          </a:gsLst>
          <a:lin ang="16200000" scaled="0"/>
        </a:gradFill>
      </a:fillStyleLst>
      <a:lnStyleLst>
        <a:ln w="9525" cap="flat" cmpd="sng" algn="ctr"><a:solidFill><a:schemeClr val="phClr"/></a:solidFill><a:prstDash val="solid"/></a:ln>
        <a:ln w="25400" cap="flat" cmpd="sng" algn="ctr"><a:solidFill><a:schemeClr val="phClr"/></a:solidFill><a:prstDash val="solid"/></a:ln>
        <a:ln w="38100" cap="flat" cmpd="sng" algn="ctr"><a:solidFill><a:schemeClr val="phClr"/></a:solidFill><a:prstDash val="solid"/></a:ln>
      </a:lnStyleLst>
      <a:effectStyleLst>
        <a:effectStyle><a:effectLst/></a:effectStyle>
        <a:effectStyle><a:effectLst/></a:effectStyle>
        <a:effectStyle><a:effectLst/></a:effectStyle>
      </a:effectStyleLst>
      <a:bgFillStyleLst>
        <a:solidFill><a:schemeClr val="phClr"/></a:solidFill>
        <a:gradFill rotWithShape="1">
          <a:gsLst>
            <a:gs pos="0"><a:schemeClr val="phClr"><a:tint val="40000"/><a:satMod val="350000"/></a:schemeClr></a:gs>
            <a:gs pos="40000"><a:schemeClr val="phClr"><a:tint val="45000"/><a:shade val="99000"/><a:satMod val="350000"/></a:schemeClr></a:gs>
            <a:gs pos="100000"><a:schemeClr val="phClr"><a:shade val="20000"/><a:satMod val="255000"/></a:schemeClr></a:gs>
          </a:gsLst>
          <a:path path="circle"><a:fillToRect l="50000" t="-80000" r="50000" b="180000"/></a:path>
        </a:gradFill>
        <a:gradFill rotWithShape="1">
          <a:gsLst>
            <a:gs pos="0"><a:schemeClr val="phClr"><a:tint val="80000"/><a:satMod val="300000"/></a:schemeClr></a:gs>
            <a:gs pos="100000"><a:schemeClr val="phClr"><a:shade val="30000"/><a:satMod val="200000"/></a:schemeClr></a:gs>
          </a:gsLst>
          <a:path path="circle"><a:fillToRect l="50000" t="50000" r="50000" b="50000"/></a:path>
        </a:gradFill>
      </a:bgFillStyleLst>
    </a:fmtScheme>
  </a:themeElements>
  <a:objectDefaults/>
  <a:extraClrSchemeLst/>
</a:theme>
"""


SLIDE_MASTER_XML = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<p:sldMaster xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main"
             xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"
             xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main">
  <p:cSld name="Slide Master">
    <p:spTree>
      <p:nvGrpSpPr><p:cNvPr id="1" name=""/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr>
      <p:grpSpPr>
        <a:xfrm><a:off x="0" y="0"/><a:ext cx="0" cy="0"/><a:chOff x="0" y="0"/><a:chExt cx="0" cy="0"/></a:xfrm>
      </p:grpSpPr>
    </p:spTree>
  </p:cSld>
  <p:clrMap bg1="lt1" tx1="dk1" bg2="lt2" tx2="dk2" accent1="accent1" accent2="accent2" accent3="accent3" accent4="accent4" accent5="accent5" accent6="accent6" hlink="hlink" folHlink="folHlink"/>
  <p:sldLayoutIdLst>
    <p:sldLayoutId id="2147483649" r:id="rId1"/>
  </p:sldLayoutIdLst>
  <p:txStyles>
    <p:titleStyle>
      <a:lvl1pPr algn="l"><a:defRPr sz="3000" b="1"/></a:lvl1pPr>
    </p:titleStyle>
    <p:bodyStyle>
      <a:lvl1pPr marL="0" indent="0"><a:defRPr sz="1800"/></a:lvl1pPr>
    </p:bodyStyle>
    <p:otherStyle>
      <a:lvl1pPr marL="0" indent="0"><a:defRPr sz="1600"/></a:lvl1pPr>
    </p:otherStyle>
  </p:txStyles>
</p:sldMaster>
"""


SLIDE_MASTER_RELS_XML = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
  <Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideLayout" Target="../slideLayouts/slideLayout1.xml"/>
  <Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/theme" Target="../theme/theme1.xml"/>
</Relationships>
"""


SLIDE_LAYOUT_XML = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<p:sldLayout xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main"
             xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"
             xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main"
             type="blank" preserve="1">
  <p:cSld name="Blank">
    <p:spTree>
      <p:nvGrpSpPr><p:cNvPr id="1" name=""/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr>
      <p:grpSpPr>
        <a:xfrm><a:off x="0" y="0"/><a:ext cx="0" cy="0"/><a:chOff x="0" y="0"/><a:chExt cx="0" cy="0"/></a:xfrm>
      </p:grpSpPr>
    </p:spTree>
  </p:cSld>
  <p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr>
</p:sldLayout>
"""


SLIDE_LAYOUT_RELS_XML = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
  <Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideMaster" Target="../slideMasters/slideMaster1.xml"/>
</Relationships>
"""


def content_types_xml(slide_count: int) -> str:
    slide_overrides = "".join(
        f"<Override PartName=\"/ppt/slides/slide{i}.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.slide+xml\"/>"
        for i in range(1, slide_count + 1)
    )
    return (
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Default Extension=\"png\" ContentType=\"image/png\"/>"
        "<Default Extension=\"jpg\" ContentType=\"image/jpeg\"/>"
        "<Default Extension=\"jpeg\" ContentType=\"image/jpeg\"/>"
        "<Override PartName=\"/ppt/presentation.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml\"/>"
        "<Override PartName=\"/ppt/slideMasters/slideMaster1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.slideMaster+xml\"/>"
        "<Override PartName=\"/ppt/slideLayouts/slideLayout1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.slideLayout+xml\"/>"
        "<Override PartName=\"/ppt/theme/theme1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.theme+xml\"/>"
        f"{slide_overrides}"
        "<Override PartName=\"/docProps/core.xml\" ContentType=\"application/vnd.openxmlformats-package.core-properties+xml\"/>"
        "<Override PartName=\"/docProps/app.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.extended-properties+xml\"/>"
        "</Types>"
    )


def root_rels_xml() -> str:
    return (
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"ppt/presentation.xml\"/>"
        "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties\" Target=\"docProps/core.xml\"/>"
        "<Relationship Id=\"rId3\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties\" Target=\"docProps/app.xml\"/>"
        "</Relationships>"
    )


def presentation_xml(slide_count: int) -> str:
    slide_ids = "".join(
        f"<p:sldId id=\"{255+i}\" r:id=\"rId{i+1}\"/>" for i in range(1, slide_count + 1)
    )
    return (
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<p:presentation xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
        "xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\" saveSubsetFonts=\"1\" autoCompressPictures=\"0\">"
        "<p:sldMasterIdLst><p:sldMasterId id=\"2147483648\" r:id=\"rId1\"/></p:sldMasterIdLst>"
        f"<p:sldIdLst>{slide_ids}</p:sldIdLst>"
        f"<p:sldSz cx=\"{SLIDE_W}\" cy=\"{SLIDE_H}\" type=\"screen16x9\"/>"
        "<p:notesSz cx=\"6858000\" cy=\"9144000\"/>"
        "<p:defaultTextStyle>"
        "<a:defPPr/>"
        "<a:lvl1pPr marL=\"0\" indent=\"0\"><a:defRPr sz=\"1800\"/></a:lvl1pPr>"
        "<a:lvl2pPr marL=\"457200\" indent=\"0\"><a:defRPr sz=\"1600\"/></a:lvl2pPr>"
        "</p:defaultTextStyle>"
        "</p:presentation>"
    )


def presentation_rels_xml(slide_count: int) -> str:
    rels = [
        (
            "rId1",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideMaster",
            "slideMasters/slideMaster1.xml",
        )
    ]
    for i in range(1, slide_count + 1):
        rels.append(
            (
                f"rId{i+1}",
                "http://schemas.openxmlformats.org/officeDocument/2006/relationships/slide",
                f"slides/slide{i}.xml",
            )
        )
    rel_xml = "".join(
        f"<Relationship Id=\"{rid}\" Type=\"{rtype}\" Target=\"{target}\"/>"
        for rid, rtype, target in rels
    )
    return (
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        f"{rel_xml}</Relationships>"
    )


def core_xml() -> str:
    now = datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")
    return (
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" "
        "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
        "xmlns:dcterms=\"http://purl.org/dc/terms/\" "
        "xmlns:dcmitype=\"http://purl.org/dc/dcmitype/\" "
        "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">"
        "<dc:title>基于ESP32的农业智能助手答辩PPT</dc:title>"
        "<dc:creator>OpenAI Codex</dc:creator>"
        "<cp:lastModifiedBy>OpenAI Codex</cp:lastModifiedBy>"
        f"<dcterms:created xsi:type=\"dcterms:W3CDTF\">{now}</dcterms:created>"
        f"<dcterms:modified xsi:type=\"dcterms:W3CDTF\">{now}</dcterms:modified>"
        "</cp:coreProperties>"
    )


def app_xml(slide_count: int) -> str:
    titles = [
        "封面",
        "作品目的与设计选择",
        "研究背景与课题意义",
        "研究现状与本文工作",
        "系统方案设计",
        "系统总体架构",
        "硬件设计",
        "软件设计与关键任务",
        "AP配网流程图",
        "系统调试与测试",
        "数据上传流程图",
        "测试结果总览",
        "关键测试结论",
        "OTA升级流程图",
        "总结与展望",
    ]
    heading_pairs = f"<vt:vector size=\"2\" baseType=\"variant\"><vt:variant><vt:lpstr>标题</vt:lpstr></vt:variant><vt:variant><vt:i4>{slide_count}</vt:i4></vt:variant></vt:vector>"
    title_vector = "".join(f"<vt:lpstr>{escape(t)}</vt:lpstr>" for t in titles[:slide_count])
    return (
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Properties xmlns=\"http://schemas.openxmlformats.org/officeDocument/2006/extended-properties\" "
        "xmlns:vt=\"http://schemas.openxmlformats.org/officeDocument/2006/docPropsVTypes\">"
        "<Application>Microsoft Office PowerPoint</Application>"
        f"<Slides>{slide_count}</Slides>"
        "<PresentationFormat>On-screen Show (16:9)</PresentationFormat>"
        "<Company>OpenAI</Company>"
        "<HeadingPairs>"
        f"{heading_pairs}"
        "</HeadingPairs>"
        "<TitlesOfParts>"
        f"<vt:vector size=\"{slide_count}\" baseType=\"lpstr\">{title_vector}</vt:vector>"
        "</TitlesOfParts>"
        "</Properties>"
    )


def build_slides() -> list[tuple[str, str, bool]]:
    slides: list[tuple[str, str, bool]] = []
    slides.append((cover_slide(), slide_rels(with_image=True), True))
    slides.append(
        (
            agenda_photo_slide("02 / 15"),
            slide_rels(with_image=True),
            True,
        )
    )
    slides.append(
        (
            focus_quote_slide(
                "研究背景与意义",
                "PROBLEM FIRST",
                "农业现场\n监测分散\n维护成本高",
                [
                    "农业现场监测点位本来就比较分散，单靠人工巡检和手工记录，效率会明显偏低",
                    "环境参数变化快，传统方式很难同时保证数据连续性、反馈及时性和后续维护效率",
                    "所以系统设计不能只顾采集，还得把上传、联网和后期维护一起考虑进去",
                ],
                "本课题真正想做的，不是把几个模块简单拼起来，而是围绕农业现场把感知、传输、平台交互和设备维护尽量连成一条完整链路",
                "03 / 15",
                "1C4A7F",
                "76D0AB",
            ),
            slide_rels(),
            False,
        )
    )
    slides.append(
        (
            band_story_slide(
                "研究现状与本文工作",
                [
                    ("已有研究基础", "LoRa 适合农业现场低功耗、远距离和多节点接入，农业监测系统也正在从单点采集逐步转向平台化、可视化和远程管理", "1F5EAB"),
                    ("本文目标", "以 ESP32 为核心，把 LoRa 本地接入、WiFi 主链路、Cat1 备份联网、MQTT 平台交互、本地存储和 OTA 维护能力整合进同一套原型系统", "38A37A"),
                    ("主要完成内容", "完成硬件原型搭建、土壤与空气数据采集、LoRa 子节点接入、平台上报链路联调，以及网关本体 OTA 主流程接入", "C8872D"),
                ],
                "04 / 15",
            ),
            slide_rels(),
            False,
        )
    )
    slides.append((section_slide("系统方案设计", "围绕总体架构、硬件组织、软件任务和系统主流程展开", "05 / 15", "01"), slide_rels(with_image=True), True))
    slides.append(
        (architecture_slide("06 / 15"), slide_rels(), False)
    )
    slides.append(
        (
            hardware_photo_slide("07 / 15"),
            slide_rels(media_rels=[("rId2", "../media/image2.jpg"), ("rId3", "../media/image3.jpg")]),
            False,
        )
    )
    slides.append(
        (
            process_slide(
                "软件设计与关键任务",
                [
                    ("系统启动", "app_main 完成 NVS、GPIO、WiFi、LoRa 与传感器初始化"),
                    ("AP 配网", "无历史 WiFi 时开启热点与网页配置，保存后自动重连目标网络"),
                    ("主备联网", "WiFi 优先启动 MQTT，长时间未连通时再进入 Cat1 备份链路"),
                    ("数据与维护", "统一上传空气、土壤和节点数据，同时执行 OTA 检测与升级触发"),
                ],
                "08 / 15",
            ),
            slide_rels(),
            False,
        )
    )
    slides.append(
        (
            ap_provisioning_flow_slide("09 / 15"),
            slide_rels(media_rels=[("rId2", "../media/image4.jpg")]),
            False,
        )
    )
    slides.append((section_slide("系统调试与测试", "重点展示平台上报链路、测试结果和 OTA 实现状态", "10 / 15", "02"), slide_rels(with_image=True), True))
    slides.append(
        (data_upload_flow_slide("11 / 15"), slide_rels(), False)
    )
    slides.append((test_dashboard_slide("12 / 15"), slide_rels(), False))
    slides.append(
        (
            band_story_slide(
                "关键测试结论",
                [
                    ("基础功能", "NVS 初始化、GPIO 与供电控制、WiFi、LoRa、土壤传感器以及 OTA 写任务创建均能正常完成，基础测试 7 项全部通过", "1F5EAB"),
                    ("通信与平台", "LoRa 查询/接收/解析、WiFi AP 配网、MQTT 主链路、Cat1 备份链路和平台字段解析均验证通过，通信测试 7 项、平台测试 6 项整体正常", "38A37A"),
                    ("实验结果", "平台可以稳定显示网关空气、土壤传感器和 LoRa 子节点三类数据，移动端也能够查看设备状态与环境参数", "C8872D"),
                ],
                "13 / 15",
            ),
            slide_rels(),
            False,
        )
    )
    slides.append((ota_upgrade_flow_slide("14 / 15"), slide_rels(), False))
    slides.append((summary_slide("15 / 15"), slide_rels(), False))
    return slides


def build_pptx() -> Path:
    if not COVER_IMAGE.exists():
        raise FileNotFoundError(f"Cover image not found: {COVER_IMAGE}")
    for extra_image in [GATEWAY_IMAGE, NODE_IMAGE, AP_PAGE_IMAGE]:
        if not extra_image.exists():
            raise FileNotFoundError(f"Required image not found: {extra_image}")

    slides = build_slides()
    slide_count = len(slides)

    with zipfile.ZipFile(OUTPUT_PPTX, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        zf.writestr("[Content_Types].xml", content_types_xml(slide_count))
        zf.writestr("_rels/.rels", root_rels_xml())
        zf.writestr("docProps/core.xml", core_xml())
        zf.writestr("docProps/app.xml", app_xml(slide_count))
        zf.writestr("ppt/presentation.xml", presentation_xml(slide_count))
        zf.writestr("ppt/_rels/presentation.xml.rels", presentation_rels_xml(slide_count))
        zf.writestr("ppt/slideMasters/slideMaster1.xml", SLIDE_MASTER_XML)
        zf.writestr("ppt/slideMasters/_rels/slideMaster1.xml.rels", SLIDE_MASTER_RELS_XML)
        zf.writestr("ppt/slideLayouts/slideLayout1.xml", SLIDE_LAYOUT_XML)
        zf.writestr("ppt/slideLayouts/_rels/slideLayout1.xml.rels", SLIDE_LAYOUT_RELS_XML)
        zf.writestr("ppt/theme/theme1.xml", THEME_XML)
        zf.write(COVER_IMAGE, arcname="ppt/media/image1.png")
        zf.write(GATEWAY_IMAGE, arcname="ppt/media/image2.jpg")
        zf.write(NODE_IMAGE, arcname="ppt/media/image3.jpg")
        zf.write(AP_PAGE_IMAGE, arcname="ppt/media/image4.jpg")

        for idx, (slide_body, rels_body, _uses_image) in enumerate(slides, start=1):
            zf.writestr(f"ppt/slides/slide{idx}.xml", slide_body)
            zf.writestr(f"ppt/slides/_rels/slide{idx}.xml.rels", rels_body)

    return OUTPUT_PPTX


if __name__ == "__main__":
    out = build_pptx()
    print(out)
