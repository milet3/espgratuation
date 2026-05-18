$ErrorActionPreference = 'Stop'

$outputDir = Join-Path $PSScriptRoot '毕业论文指导记录材料'
if (-not (Test-Path $outputDir)) {
    New-Item -ItemType Directory -Path $outputDir | Out-Null
}

$htmlPath = Join-Path $outputDir '毕业论文指导记录.html'
$pdfPath = Join-Path $outputDir '毕业论文指导记录.pdf'

$items = @(
    @{
        Date = '2026年5月7日'
        Stage = '第一次查重与系统提交提醒'
        Description = '指导教师提醒本人尽快在毕业论文系统内提交论文开展首次查重，避免错过截止时间，并强调要充分利用系统提供的查重机会，尽早发现论文中的重复内容。'
        ImagePath = 'C:\Users\Milet\Desktop\毕设资料\指导记录\Screenshot_2026-05-18-15-10-02-454_com.tencent.mm.jpg'
    },
    @{
        Date = '2026年5月7日-5月8日'
        Stage = '查重结果反馈与继续修改要求'
        Description = '在完成阶段性修改后，指导教师反馈当前重复率整体可控，但论文仍存在较多需要完善之处，要求本人继续补充内容、抓紧修改，并在修改后再次进行查重。'
        ImagePath = 'C:\Users\Milet\Desktop\毕设资料\指导记录\Screenshot_2026-05-18-15-09-55-702_com.tencent.mm.jpg'
    },
    @{
        Date = '2026年5月9日'
        Stage = '论文结构与格式修改意见'
        Description = '指导教师审阅论文后指出，论文中仍有较多修改意见未落实，需补充平台页面或相关配图，并严格参照学校论文模板调整版式和格式，提高论文规范性。'
        ImagePath = 'C:\Users\Milet\Desktop\毕设资料\指导记录\Screenshot_2026-05-18-15-09-48-551_com.tencent.mm.jpg'
    },
    @{
        Date = '2026年5月10日-5月11日'
        Stage = '章节内容完善与暂缓盲审提交'
        Description = '指导教师进一步要求本人在第三章、第四章各节中增加必要配图，减少单纯文字描述，同时明确指出论文尚未定稿，暂不具备提交盲审版条件，应继续完善后再提交。'
        ImagePath = 'C:\Users\Milet\Desktop\毕设资料\指导记录\Screenshot_2026-05-18-15-09-40-289_com.tencent.mm.jpg'
    },
    @{
        Date = '2026年5月16日 上午'
        Stage = '批注修改与盲审版准备'
        Description = '本人提交根据前期意见修改后的论文版本，指导教师将带有批注和修订痕迹的论文返回，要求本人继续依据修改意见完善内容，并提醒在提交盲审版前做好个人信息脱敏处理。'
        ImagePath = 'C:\Users\Milet\Desktop\毕设资料\指导记录\Screenshot_2026-05-18-15-09-16-349_com.tencent.mm.jpg'
    },
    @{
        Date = '2026年5月16日 上午'
        Stage = 'AIGC 检测要求说明'
        Description = '针对盲审版提交要求，指导教师明确提出需在提交前进行 AIGC 检测，并将相关指标控制在学校要求范围内，以免因检测结果不达标影响后续答辩资格。'
        ImagePath = 'C:\Users\Milet\Desktop\毕设资料\指导记录\Screenshot_2026-05-18-15-09-20-804_com.tencent.mm.jpg'
    },
    @{
        Date = '2026年5月16日 11:32'
        Stage = '检测结果沟通与再次优化'
        Description = '本人向指导教师反馈 AIGC 检测结果后，指导教师结合学校要求进行判断，认为当前结果基本处于可接受范围，但仍建议根据检测结果继续优化论文表述，进一步降低提交风险。'
        ImagePath = 'C:\Users\Milet\Desktop\毕设资料\指导记录\Screenshot_2026-05-18-15-09-26-200_com.tencent.mm.jpg'
    },
    @{
        Date = '2026年5月16日 中午'
        Stage = '最终确认提交与答辩准备'
        Description = '本人根据意见进一步调整后，再次向指导教师确认是否可以上传。指导教师明确回复可以提交，系统页面显示论文已提交并审核通过，同时指导教师要求本人尽快准备毕业答辩 PPT。'
        ImagePath = 'C:\Users\Milet\Desktop\毕设资料\指导记录\Screenshot_2026-05-18-15-09-31-584_com.tencent.mm.jpg'
    }
)

$cards = foreach ($item in $items) {
    if (-not (Test-Path $item.ImagePath)) {
        throw "未找到截图文件：$($item.ImagePath)"
    }

    $imageUri = ([System.Uri]$item.ImagePath).AbsoluteUri
    @"
    <section class="page">
      <div class="meta">$($item.Date)</div>
      <h2>$($item.Stage)</h2>
      <div class="shot-wrap">
        <img src="$imageUri" alt="$($item.Stage)">
      </div>
      <p>$($item.Description)</p>
    </section>
"@
}

$html = @"
<!DOCTYPE html>
<html lang="zh-CN">
<head>
  <meta charset="UTF-8">
  <title>毕业论文指导记录</title>
  <style>
    @page {
      size: A4;
      margin: 14mm 12mm;
    }
    * {
      box-sizing: border-box;
    }
    body {
      margin: 0;
      font-family: "Microsoft YaHei", "PingFang SC", sans-serif;
      color: #1f2937;
      background: #ffffff;
    }
    .cover, .intro, .page {
      page-break-after: always;
      min-height: 268mm;
    }
    .cover {
      display: flex;
      flex-direction: column;
      justify-content: center;
      align-items: center;
      text-align: center;
      padding: 22mm 12mm;
    }
    .cover h1 {
      margin: 0 0 18mm;
      font-size: 26pt;
      letter-spacing: 1px;
    }
    .cover .sub {
      font-size: 12pt;
      color: #4b5563;
      line-height: 1.9;
      max-width: 150mm;
    }
    .intro {
      padding: 6mm 2mm;
    }
    h2, h3 {
      margin: 0 0 6mm;
    }
    .intro h2 {
      font-size: 18pt;
      margin-bottom: 8mm;
    }
    .intro p, .intro li, .page p {
      font-size: 11.5pt;
      line-height: 1.8;
    }
    .intro ul {
      padding-left: 20px;
      margin: 0;
    }
    .page {
      display: flex;
      flex-direction: column;
      gap: 4mm;
      padding: 2mm 0;
    }
    .meta {
      font-size: 10.5pt;
      color: #6b7280;
    }
    .page h2 {
      font-size: 17pt;
      margin-bottom: 1mm;
      color: #111827;
    }
    .shot-wrap {
      flex: 1;
      display: flex;
      align-items: center;
      justify-content: center;
      border: 1px solid #e5e7eb;
      border-radius: 10px;
      padding: 4mm;
      background: #fafafa;
      overflow: hidden;
    }
    .shot-wrap img {
      max-width: 100%;
      max-height: 188mm;
      width: auto;
      height: auto;
      object-fit: contain;
      border-radius: 6px;
      box-shadow: 0 8px 18px rgba(0, 0, 0, 0.08);
    }
    .page p {
      margin: 0;
      text-align: justify;
    }
    .note {
      margin-top: 12mm;
      padding: 5mm 6mm;
      background: #f3f4f6;
      border-radius: 10px;
    }
  </style>
</head>
<body>
  <section class="cover">
    <h1>毕业论文指导记录</h1>
    <div class="sub">
      本材料根据本人与指导教师关于毕业论文修改、查重、盲审提交及答辩准备等事项的微信聊天记录整理形成，现按时间顺序汇总，用于毕业论文指导过程佐证材料提交。
    </div>
  </section>

  <section class="intro">
    <h2>情况说明</h2>
    <p>以下截图真实反映了本人在毕业论文撰写与修改阶段，按照指导教师要求持续开展论文内容补充、格式规范调整、重复率与 AIGC 检测、盲审版准备及系统提交等工作的过程。指导教师在聊天中针对论文存在的问题及时提出修改意见，并对查重、提交和答辩准备事项进行了明确指导。</p>
    <div class="note">
      <h3>主要指导内容</h3>
      <ul>
        <li>督促本人按照学校时间节点完成论文修改、查重和系统提交。</li>
        <li>对论文结构、章节内容、配图补充、格式规范等方面提出具体修改意见。</li>
        <li>指导本人开展重复率和 AIGC 检测，并根据检测结果继续优化论文。</li>
        <li>在确认论文达到提交条件后，同意上传，并提醒本人继续准备毕业答辩材料。</li>
      </ul>
    </div>
  </section>

$($cards -join "`r`n")

</body>
</html>
"@

[System.IO.File]::WriteAllText($htmlPath, $html, [System.Text.Encoding]::UTF8)

$edgePath = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
$chromePath = 'C:\Program Files\Google\Chrome\Application\chrome.exe'

if (Test-Path $edgePath) {
    $browserPath = $edgePath
} elseif (Test-Path $chromePath) {
    $browserPath = $chromePath
} else {
    throw '未找到可用于导出 PDF 的浏览器。'
}

$htmlUri = ([System.Uri]$htmlPath).AbsoluteUri

& $browserPath `
    --headless `
    --disable-gpu `
    --allow-file-access-from-files `
    --print-to-pdf-no-header `
    "--print-to-pdf=$pdfPath" `
    $htmlUri | Out-Null

if (-not (Test-Path $pdfPath)) {
    throw 'PDF 导出失败。'
}

Write-Output "HTML=$htmlPath"
Write-Output "PDF=$pdfPath"
