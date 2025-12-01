# Script de Compilación para ESP32-S3
# Este script configura el entorno ESP-IDF y compila el proyecto

Write-Host "=== Configuración de Proyecto ESP32-S3 ===" -ForegroundColor Cyan
Write-Host ""

# Paso 1: Verificar si existe ESP-IDF
$idfPath = "C:\Espressif\frameworks\esp-idf-v5.5.1"
if (-not (Test-Path "$idfPath\export.ps1")) {
    $idfPath = "C:\Users\$env:USERNAME\.espressif\esp-idf"
    if (-not (Test-Path "$idfPath\export.ps1")) {
        Write-Host "❌ No se encontró ESP-IDF. Por favor instala ESP-IDF v5.5.1" -ForegroundColor Red
        exit 1
    }
}

Write-Host "✅ ESP-IDF encontrado en: $idfPath" -ForegroundColor Green

# Paso 2: Importar entorno ESP-IDF
Write-Host ""
Write-Host "⚙️  Configurando entorno ESP-IDF..." -ForegroundColor Yellow
try {
    . "$idfPath\export.ps1"
    Write-Host "✅ Entorno ESP-IDF configurado correctamente" -ForegroundColor Green
} catch {
    Write-Host "❌ Error al configurar entorno ESP-IDF: $_" -ForegroundColor Red
    exit 1
}

# Paso 3: Verificar idf.py
Write-Host ""
Write-Host "🔍 Verificando herramientas..." -ForegroundColor Yellow
$idfPyVersion = idf.py --version 2>&1
if ($LASTEXITCODE -eq 0) {
    Write-Host "✅ idf.py disponible" -ForegroundColor Green
} else {
    Write-Host "❌ idf.py no disponible" -ForegroundColor Red
    exit 1
}

# Paso 4: Configurar target ESP32-S3
Write-Host ""
Write-Host "🎯 Configurando target a ESP32-S3..." -ForegroundColor Yellow
idf.py set-target esp32s3
if ($LASTEXITCODE -eq 0) {
    Write-Host "✅ Target configurado a ESP32-S3" -ForegroundColor Green
} else {
    Write-Host "❌ Error al configurar target" -ForegroundColor Red
    exit 1
}

# Paso 5: Compilar proyecto
Write-Host ""
Write-Host "🔨 Compilando proyecto..." -ForegroundColor Yellow
Write-Host ""
idf.py build

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Green
    Write-Host "✅ COMPILACIÓN EXITOSA" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "📝 Próximos pasos:" -ForegroundColor Cyan
    Write-Host "   1. Conecta tu ESP32-S3"
    Write-Host "   2. Ejecuta: idf.py flash"
    Write-Host "   3. Para ver logs: idf.py monitor"
    Write-Host "   4. O todo junto: idf.py flash monitor"
    Write-Host ""
} else {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Red
    Write-Host "❌ ERROR EN COMPILACIÓN" -ForegroundColor Red
    Write-Host "========================================" -ForegroundColor Red
    Write-Host ""
    Write-Host "🔍 Revisa los errores arriba" -ForegroundColor Yellow
    Write-Host "📖 Consulta ESP32_S3_PIN_MAP.md para detalles de pines" -ForegroundColor Yellow
    Write-Host ""
    exit 1
}
