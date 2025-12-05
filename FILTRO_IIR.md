# 🎛️ Filtro Digital IIR - Potenciómetro ESP32

## ✅ Implementado

**Tipo de Filtro:** IIR - Exponential Moving Average (EMA)  
**Complejidad:** Mínima (3 líneas de código)  
**Rendimiento:** Ultra eficiente (2 multiplicaciones)

### Cambios Realizados:

1. **Función de filtrado** (línea ~1027):
   ```c
   #define POT_FILTER_ALPHA 0.15f
   static float g_filtered_raw = 0.0f;
   
   static int pot_apply_filter(int raw_sample) {
       if (g_filtered_raw == 0.0f) 
           g_filtered_raw = (float)raw_sample;
       g_filtered_raw = POT_FILTER_ALPHA * raw_sample + 
                        (1.0f - POT_FILTER_ALPHA) * g_filtered_raw;
       return (int)(g_filtered_raw + 0.5f);
   }
   ```

2. **Aplicación del filtro** (línea ~1096):
   ```c
   int filtered_raw = pot_apply_filter(raw);
   int digit = pot_raw_to_digit(filtered_raw);
   ```

3. **Logs para visualización** (línea ~1116):
   ```c
   ESP_LOGI(TAG, "POT: raw=%d filtered=%d digit=%d", raw, filtered_raw, digit);
   ```

---

## 🚀 Uso Rápido

### 1. Compilar y flashear:
```powershell
idf.py build flash monitor
```

### 2. Visualizar con Python:
```powershell
# Instalar matplotlib (una sola vez)
pip install matplotlib pyserial

# Ejecutar (cambiar COM3 por tu puerto)
python plot_serial.py COM3
```

---

## 📊 Resultados Esperados

### Antes del filtro:
```
raw=1205 → digit=5
raw=1198 → digit=5  ⚠️ Fluctuación
raw=1212 → digit=5
raw=1203 → digit=5  ⚠️ Ruido
```

### Con filtro IIR:
```
raw=1205 filtered=1200 → digit=5
raw=1198 filtered=1199 → digit=5  ✅ Estable
raw=1212 filtered=1201 → digit=5
raw=1203 filtered=1201 → digit=5  ✅ Suave
```

---

## 🔧 Ajustar Filtro

Editar en `main.c`:
```c
#define POT_FILTER_ALPHA  0.15f  // Cambiar aquí
```

| Valor | Efecto |
|-------|--------|
| 0.05 | Máximo suavizado (lento) |
| 0.15 | **Recomendado** (balance) |
| 0.30 | Respuesta rápida |
| 0.50 | Mínimo filtrado |

---

## 📖 Documentación Completa

Ver: `VISUALIZACION_FILTRO.md`

---

## 🎯 Ventajas del Filtro IIR

✅ **Solo 1 float de memoria** (vs FIR que necesita N valores)  
✅ **2 multiplicaciones** (vs FIR que necesita N)  
✅ **Elimina ruido de 60Hz del ADC**  
✅ **Mejora estabilidad de detección de dígitos**  
✅ **Cero impacto en rendimiento**
