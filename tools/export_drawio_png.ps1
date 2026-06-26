param(
    [Parameter(Mandatory = $true)]
    [string]$InputPath,

    [Parameter(Mandatory = $true)]
    [string]$OutputDir,

    [double]$Scale = 2.0,

    [int]$Dpi = 300
)

$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Web

function Parse-Style {
    param([string]$StyleString)

    $map = @{}
    if ([string]::IsNullOrWhiteSpace($StyleString)) {
        return $map
    }

    foreach ($part in ($StyleString -split ';')) {
        if ([string]::IsNullOrWhiteSpace($part)) {
            continue
        }
        $kv = $part -split '=', 2
        if ($kv.Count -eq 2) {
            $map[$kv[0]] = $kv[1]
        } else {
            $map[$part] = '1'
        }
    }
    return $map
}

function Get-StyleValue {
    param(
        [hashtable]$Style,
        [string]$Key,
        $Default
    )

    if ($Style.ContainsKey($Key)) {
        return $Style[$Key]
    }
    return $Default
}

function Convert-HexColor {
    param(
        [string]$Hex,
        [System.Drawing.Color]$DefaultColor
    )

    if ([string]::IsNullOrWhiteSpace($Hex)) {
        return $DefaultColor
    }

    $trimmed = $Hex.Trim()
    if ($trimmed.StartsWith('#')) {
        $trimmed = $trimmed.Substring(1)
    }

    if ($trimmed.Length -eq 6) {
        return [System.Drawing.Color]::FromArgb(
            255,
            [Convert]::ToInt32($trimmed.Substring(0, 2), 16),
            [Convert]::ToInt32($trimmed.Substring(2, 2), 16),
            [Convert]::ToInt32($trimmed.Substring(4, 2), 16)
        )
    }

    if ($trimmed.Length -eq 8) {
        return [System.Drawing.Color]::FromArgb(
            [Convert]::ToInt32($trimmed.Substring(0, 2), 16),
            [Convert]::ToInt32($trimmed.Substring(2, 2), 16),
            [Convert]::ToInt32($trimmed.Substring(4, 2), 16),
            [Convert]::ToInt32($trimmed.Substring(6, 2), 16)
        )
    }

    return $DefaultColor
}

function Decode-CellText {
    param([string]$Value)

    if ([string]::IsNullOrWhiteSpace($Value)) {
        return ''
    }

    $decoded = [System.Net.WebUtility]::HtmlDecode($Value)
    $decoded = $decoded -replace '<div>', "`n"
    $decoded = $decoded -replace '</div>', ''
    $decoded = $decoded -replace '<br\s*/?>', "`n"
    $decoded = $decoded -replace '<[^>]+>', ''
    return $decoded.Trim()
}

function Get-XmlAttr {
    param(
        $Node,
        [string]$Name
    )

    if ($null -eq $Node) {
        return $null
    }

    $attr = $Node.Attributes[$Name]
    if ($null -ne $attr) {
        return [string]$attr.Value
    }

    return $null
}

function Get-FilenamePrefix {
    param([string]$PageName, [int]$Index)

    if ($PageName -match '^\d+\.\d+') {
        return $Matches[0]
    }
    return ('page_{0:00}' -f $Index)
}

function New-RoundedRectanglePath {
    param(
        [float]$X,
        [float]$Y,
        [float]$Width,
        [float]$Height,
        [float]$Radius
    )

    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $diameter = [Math]::Min([Math]::Min($Width, $Height), $Radius * 2.0)
    if ($diameter -lt 2.0) {
        $path.AddRectangle([System.Drawing.RectangleF]::new($X, $Y, $Width, $Height))
        return $path
    }

    $arc = [System.Drawing.RectangleF]::new($X, $Y, $diameter, $diameter)
    $path.AddArc($arc, 180, 90)
    $arc.X = $X + $Width - $diameter
    $path.AddArc($arc, 270, 90)
    $arc.Y = $Y + $Height - $diameter
    $path.AddArc($arc, 0, 90)
    $arc.X = $X
    $path.AddArc($arc, 90, 90)
    $path.CloseFigure()
    return $path
}

function Get-CellRect {
    param($Cell, [double]$ScaleFactor)

    $geom = $Cell.mxGeometry
    return [PSCustomObject]@{
        X = [double]$geom.x * $ScaleFactor
        Y = [double]$geom.y * $ScaleFactor
        Width = [double]$geom.width * $ScaleFactor
        Height = [double]$geom.height * $ScaleFactor
    }
}

function Get-AnchorPoint {
    param(
        [pscustomobject]$Rect,
        [string]$Side
    )

    switch ($Side) {
        'top' {
            return [System.Drawing.PointF]::new([float]($Rect.X + $Rect.Width / 2.0), [float]$Rect.Y)
        }
        'bottom' {
            return [System.Drawing.PointF]::new([float]($Rect.X + $Rect.Width / 2.0), [float]($Rect.Y + $Rect.Height))
        }
        'left' {
            return [System.Drawing.PointF]::new([float]$Rect.X, [float]($Rect.Y + $Rect.Height / 2.0))
        }
        'right' {
            return [System.Drawing.PointF]::new([float]($Rect.X + $Rect.Width), [float]($Rect.Y + $Rect.Height / 2.0))
        }
        default {
            return [System.Drawing.PointF]::new([float]($Rect.X + $Rect.Width / 2.0), [float]($Rect.Y + $Rect.Height / 2.0))
        }
    }
}

function Get-DominantSideFromStyle {
    param(
        [hashtable]$Style,
        [string]$Prefix,
        [pscustomobject]$Rect,
        [pscustomobject]$OtherRect
    )

    $xKey = "${Prefix}X"
    $yKey = "${Prefix}Y"

    if ($Style.ContainsKey($xKey) -or $Style.ContainsKey($yKey)) {
        $x = if ($Style.ContainsKey($xKey)) { [double]$Style[$xKey] } else { 0.5 }
        $y = if ($Style.ContainsKey($yKey)) { [double]$Style[$yKey] } else { 0.5 }
        $distLeft = [Math]::Abs($x - 0.0)
        $distRight = [Math]::Abs($x - 1.0)
        $distTop = [Math]::Abs($y - 0.0)
        $distBottom = [Math]::Abs($y - 1.0)
        $min = ($distLeft, $distRight, $distTop, $distBottom | Measure-Object -Minimum).Minimum
        if ($min -eq $distTop) { return 'top' }
        if ($min -eq $distBottom) { return 'bottom' }
        if ($min -eq $distLeft) { return 'left' }
        return 'right'
    }

    $dx = ($OtherRect.X + $OtherRect.Width / 2.0) - ($Rect.X + $Rect.Width / 2.0)
    $dy = ($OtherRect.Y + $OtherRect.Height / 2.0) - ($Rect.Y + $Rect.Height / 2.0)
    if ([Math]::Abs($dx) -gt [Math]::Abs($dy)) {
        return $(if ($dx -ge 0) { 'right' } else { 'left' })
    }
    return $(if ($dy -ge 0) { 'bottom' } else { 'top' })
}

function Get-GeometryPoints {
    param($Geometry, [double]$ScaleFactor)

    $points = New-Object System.Collections.Generic.List[System.Drawing.PointF]
    if ($null -eq $Geometry) {
        return $points
    }

    $arrayNode = $Geometry.SelectSingleNode('./Array[@as="points"]')
    if ($null -eq $arrayNode) {
        return $points
    }

    foreach ($pointNode in $arrayNode.SelectNodes('./mxPoint')) {
        $points.Add(
            [System.Drawing.PointF]::new(
                [float]([double]$pointNode.x * $ScaleFactor),
                [float]([double]$pointNode.y * $ScaleFactor)
            )
        )
    }
    return $points
}

function Get-Polyline {
    param(
        [System.Drawing.PointF]$Start,
        [System.Drawing.PointF]$End,
        [string]$StartSide,
        [string]$EndSide,
        [System.Collections.Generic.List[System.Drawing.PointF]]$MidPoints
    )

    $route = New-Object System.Collections.Generic.List[System.Drawing.PointF]
    $route.Add($Start)

    if ($MidPoints.Count -gt 0) {
        foreach ($point in $MidPoints) {
            $route.Add($point)
        }
        $route.Add($End)
        return $route
    }

    $startVertical = $StartSide -in @('top', 'bottom')
    $endVertical = $EndSide -in @('top', 'bottom')
    $startHorizontal = $StartSide -in @('left', 'right')
    $endHorizontal = $EndSide -in @('left', 'right')

    if ($startVertical -and $endVertical) {
        $midY = [float](($Start.Y + $End.Y) / 2.0)
        $route.Add([System.Drawing.PointF]::new($Start.X, $midY))
        $route.Add([System.Drawing.PointF]::new($End.X, $midY))
    } elseif ($startHorizontal -and $endHorizontal) {
        $midX = [float](($Start.X + $End.X) / 2.0)
        $route.Add([System.Drawing.PointF]::new($midX, $Start.Y))
        $route.Add([System.Drawing.PointF]::new($midX, $End.Y))
    } elseif ($startVertical -and $endHorizontal) {
        $route.Add([System.Drawing.PointF]::new($Start.X, $End.Y))
    } elseif ($startHorizontal -and $endVertical) {
        $route.Add([System.Drawing.PointF]::new($End.X, $Start.Y))
    } else {
        $midY = [float](($Start.Y + $End.Y) / 2.0)
        $route.Add([System.Drawing.PointF]::new($Start.X, $midY))
        $route.Add([System.Drawing.PointF]::new($End.X, $midY))
    }

    $route.Add($End)
    return $route
}

function Draw-Vertex {
    param(
        [System.Drawing.Graphics]$Graphics,
        $Cell,
        [double]$ScaleFactor
    )

    $style = Parse-Style (Get-XmlAttr -Node $Cell -Name 'style')
    $rect = Get-CellRect -Cell $Cell -ScaleFactor $ScaleFactor
    $fillColor = Convert-HexColor -Hex (Get-StyleValue $style 'fillColor' '#FFFFFF') -DefaultColor ([System.Drawing.Color]::White)
    $strokeColor = Convert-HexColor -Hex (Get-StyleValue $style 'strokeColor' '#111827') -DefaultColor ([System.Drawing.Color]::Black)
    $fontColor = Convert-HexColor -Hex (Get-StyleValue $style 'fontColor' '#111827') -DefaultColor ([System.Drawing.Color]::Black)
    $fontSize = [float](Get-StyleValue $style 'fontSize' 18) * [float]$ScaleFactor
    $strokeWidth = [float](Get-StyleValue $style 'strokeWidth' 2) * [float]$ScaleFactor
    $fontStyle = [System.Drawing.FontStyle]::Regular
    if ((Get-StyleValue $style 'fontStyle' '0') -eq '1') {
        $fontStyle = [System.Drawing.FontStyle]::Bold
    }

    $brush = New-Object System.Drawing.SolidBrush($fillColor)
    $pen = New-Object System.Drawing.Pen($strokeColor, $strokeWidth)
    if ((Get-StyleValue $style 'dashed' '0') -eq '1') {
        $pen.DashStyle = [System.Drawing.Drawing2D.DashStyle]::Dash
    }

    try {
        if ($style.ContainsKey('ellipse')) {
            $Graphics.FillEllipse($brush, [float]$rect.X, [float]$rect.Y, [float]$rect.Width, [float]$rect.Height)
            $Graphics.DrawEllipse($pen, [float]$rect.X, [float]$rect.Y, [float]$rect.Width, [float]$rect.Height)
        } elseif ((Get-StyleValue $style 'rhombus' '0') -eq '1') {
            $points = @(
                [System.Drawing.PointF]::new([float]($rect.X + $rect.Width / 2.0), [float]$rect.Y),
                [System.Drawing.PointF]::new([float]($rect.X + $rect.Width), [float]($rect.Y + $rect.Height / 2.0)),
                [System.Drawing.PointF]::new([float]($rect.X + $rect.Width / 2.0), [float]($rect.Y + $rect.Height)),
                [System.Drawing.PointF]::new([float]$rect.X, [float]($rect.Y + $rect.Height / 2.0))
            )
            $Graphics.FillPolygon($brush, $points)
            $Graphics.DrawPolygon($pen, $points)
        } else {
            $radius = 18.0 * $ScaleFactor
            $path = New-RoundedRectanglePath -X $rect.X -Y $rect.Y -Width $rect.Width -Height $rect.Height -Radius $radius
            try {
                $Graphics.FillPath($brush, $path)
                $Graphics.DrawPath($pen, $path)
            } finally {
                $path.Dispose()
            }
        }

        $text = Decode-CellText (Get-XmlAttr -Node $Cell -Name 'value')
        if (-not [string]::IsNullOrWhiteSpace($text)) {
            $textRect = [System.Drawing.RectangleF]::new(
                [float]($rect.X + 12 * $ScaleFactor),
                [float]($rect.Y + 8 * $ScaleFactor),
                [float]($rect.Width - 24 * $ScaleFactor),
                [float]($rect.Height - 16 * $ScaleFactor)
            )
            $font = New-Object System.Drawing.Font('Microsoft YaHei', $fontSize, $fontStyle, [System.Drawing.GraphicsUnit]::Pixel)
            $textBrush = New-Object System.Drawing.SolidBrush($fontColor)
            $format = New-Object System.Drawing.StringFormat
            $format.Alignment = [System.Drawing.StringAlignment]::Center
            $format.LineAlignment = [System.Drawing.StringAlignment]::Center
            $format.FormatFlags = [System.Drawing.StringFormatFlags]::NoClip
            try {
                $Graphics.DrawString($text, $font, $textBrush, $textRect, $format)
            } finally {
                $format.Dispose()
                $textBrush.Dispose()
                $font.Dispose()
            }
        }
    } finally {
        $pen.Dispose()
        $brush.Dispose()
    }
}

function Draw-ArrowHead {
    param(
        [System.Drawing.Graphics]$Graphics,
        [System.Drawing.PointF]$BeforeEnd,
        [System.Drawing.PointF]$End,
        [System.Drawing.Color]$Color,
        [float]$Size
    )

    $dx = $End.X - $BeforeEnd.X
    $dy = $End.Y - $BeforeEnd.Y
    $length = [Math]::Sqrt(($dx * $dx) + ($dy * $dy))
    if ($length -le 0.1) {
        return
    }

    $ux = $dx / $length
    $uy = $dy / $length
    $px = -$uy
    $py = $ux

    $p1 = $End
    $p2 = [System.Drawing.PointF]::new(
        [float]($End.X - $ux * $Size + $px * ($Size * 0.45)),
        [float]($End.Y - $uy * $Size + $py * ($Size * 0.45))
    )
    $p3 = [System.Drawing.PointF]::new(
        [float]($End.X - $ux * $Size - $px * ($Size * 0.45)),
        [float]($End.Y - $uy * $Size - $py * ($Size * 0.45))
    )

    $brush = New-Object System.Drawing.SolidBrush($Color)
    try {
        $Graphics.FillPolygon($brush, @($p1, $p2, $p3))
    } finally {
        $brush.Dispose()
    }
}

function Draw-Edge {
    param(
        [System.Drawing.Graphics]$Graphics,
        $EdgeCell,
        [hashtable]$VertexMap,
        [double]$ScaleFactor
    )

    $sourceId = Get-XmlAttr -Node $EdgeCell -Name 'source'
    $targetId = Get-XmlAttr -Node $EdgeCell -Name 'target'
    if ([string]::IsNullOrWhiteSpace($sourceId) -or [string]::IsNullOrWhiteSpace($targetId)) {
        return
    }

    if (-not $VertexMap.ContainsKey($sourceId) -or -not $VertexMap.ContainsKey($targetId)) {
        return
    }

    $source = $VertexMap[$sourceId]
    $target = $VertexMap[$targetId]
    $sourceRect = Get-CellRect -Cell $source -ScaleFactor $ScaleFactor
    $targetRect = Get-CellRect -Cell $target -ScaleFactor $ScaleFactor
    $style = Parse-Style (Get-XmlAttr -Node $EdgeCell -Name 'style')

    $sourceSide = Get-DominantSideFromStyle -Style $style -Prefix 'exit' -Rect $sourceRect -OtherRect $targetRect
    $targetSide = Get-DominantSideFromStyle -Style $style -Prefix 'entry' -Rect $targetRect -OtherRect $sourceRect

    $start = Get-AnchorPoint -Rect $sourceRect -Side $sourceSide
    $end = Get-AnchorPoint -Rect $targetRect -Side $targetSide
    $midPoints = Get-GeometryPoints -Geometry $EdgeCell.mxGeometry -ScaleFactor $ScaleFactor
    $route = Get-Polyline -Start $start -End $end -StartSide $sourceSide -EndSide $targetSide -MidPoints $midPoints

    $strokeColor = Convert-HexColor -Hex (Get-StyleValue $style 'strokeColor' '#475569') -DefaultColor ([System.Drawing.Color]::Black)
    $strokeWidth = [float](Get-StyleValue $style 'strokeWidth' 2) * [float]$ScaleFactor
    $pen = New-Object System.Drawing.Pen($strokeColor, $strokeWidth)
    $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    if ((Get-StyleValue $style 'dashed' '0') -eq '1') {
        $pen.DashStyle = [System.Drawing.Drawing2D.DashStyle]::Dash
    }

    try {
        for ($i = 0; $i -lt ($route.Count - 1); $i++) {
            $Graphics.DrawLine($pen, $route[$i], $route[$i + 1])
        }

        if ((Get-StyleValue $style 'endArrow' 'block') -ne 'none') {
            Draw-ArrowHead -Graphics $Graphics -BeforeEnd $route[$route.Count - 2] -End $route[$route.Count - 1] -Color $strokeColor -Size ([float](11 * $ScaleFactor))
        }

        $label = Decode-CellText (Get-XmlAttr -Node $EdgeCell -Name 'value')
        if (-not [string]::IsNullOrWhiteSpace($label)) {
            $midIndex = [Math]::Floor(($route.Count - 1) / 2)
            $pA = $route[$midIndex]
            $pB = $route[[Math]::Min($midIndex + 1, $route.Count - 1)]
            $labelPoint = [System.Drawing.PointF]::new([float](($pA.X + $pB.X) / 2.0), [float](($pA.Y + $pB.Y) / 2.0))
            $fontSize = [float](Get-StyleValue $style 'fontSize' 18) * [float]$ScaleFactor
            $font = New-Object System.Drawing.Font('Microsoft YaHei', $fontSize, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
            $textBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 31, 41, 55))
            $bgBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::White)
            try {
                $size = $Graphics.MeasureString($label, $font)
                $padding = 6 * $ScaleFactor
                $rect = [System.Drawing.RectangleF]::new(
                    [float]($labelPoint.X - ($size.Width / 2.0) - $padding),
                    [float]($labelPoint.Y - ($size.Height / 2.0) - $padding / 2.0),
                    [float]($size.Width + $padding * 2.0),
                    [float]($size.Height + $padding)
                )
                $Graphics.FillRectangle($bgBrush, $rect)
                $Graphics.DrawString(
                    $label,
                    $font,
                    $textBrush,
                    [System.Drawing.PointF]::new([float]($rect.X + $padding), [float]($rect.Y + $padding / 4.0))
                )
            } finally {
                $bgBrush.Dispose()
                $textBrush.Dispose()
                $font.Dispose()
            }
        }
    } finally {
        $pen.Dispose()
    }
}

$resolvedInput = (Resolve-Path -LiteralPath $InputPath).Path
if (-not (Test-Path -LiteralPath $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
}

$content = Get-Content -LiteralPath $resolvedInput -Raw -Encoding UTF8
[xml]$xml = $content

$diagramIndex = 0
foreach ($diagram in $xml.mxfile.diagram) {
    $diagramIndex++
    $graphModel = $diagram.mxGraphModel
    $pageWidth = [int]([double]$graphModel.pageWidth * $Scale)
    $pageHeight = [int]([double]$graphModel.pageHeight * $Scale)

    $bitmap = New-Object System.Drawing.Bitmap($pageWidth, $pageHeight, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $bitmap.SetResolution($Dpi, $Dpi)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    $graphics.Clear([System.Drawing.Color]::White)

    try {
        $rootNode = $graphModel.root
        $cells = @($rootNode.mxCell)
        $vertexCells = @()
        $edgeCells = @()
        $vertexMap = @{}

        foreach ($cell in $cells) {
            if ((Get-XmlAttr -Node $cell -Name 'vertex') -eq '1') {
                $vertexCells += $cell
                $vertexMap[(Get-XmlAttr -Node $cell -Name 'id')] = $cell
            } elseif ((Get-XmlAttr -Node $cell -Name 'edge') -eq '1') {
                $edgeCells += $cell
            }
        }

        foreach ($edgeCell in $edgeCells) {
            Draw-Edge -Graphics $graphics -EdgeCell $edgeCell -VertexMap $vertexMap -ScaleFactor $Scale
        }

        foreach ($vertexCell in $vertexCells) {
            Draw-Vertex -Graphics $graphics -Cell $vertexCell -ScaleFactor $Scale
        }

        $prefix = Get-FilenamePrefix -PageName ([string]$diagram.name) -Index $diagramIndex
        $outputPath = Join-Path $OutputDir ($prefix + '.png')
        $bitmap.Save($outputPath, [System.Drawing.Imaging.ImageFormat]::Png)
        Write-Output $outputPath
    } finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}
