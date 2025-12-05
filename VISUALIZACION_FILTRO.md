# Visualización de Datos Filtrados - ESP-IDF en VS Code

## 🎯 Filtro Implementado

**Tipo:** IIR - Exponential Moving Average (EMA)  
**Fórmula:** `y[n] = α·x[n] + (1-α)·y[n-1]`  
**Alpha:** 0.15 (configurable en `POT_FILTER_ALPHA`)

### Ventajas del Filtro IIR:
- ✅ **Mínima memoria**: Solo almacena 1 valor previo
- ✅ **Ultra eficiente**: 2 multiplicaciones + 1 suma
- ✅ **Suaviza ruido**: Elimina fluctuaciones rápidas del ADC
- ✅ **Tiempo real**: Latencia mínima (~150ms con α=0.15)

---

## 📊 Método 1: Serial Plotter (Integrado en VS Code)

### Extensión Recomendada: **Serial Monitor**
1. Instalar: `Ctrl+P` → `ext install ms-vscode.vscode-serial-monitor`
2. Abrir: `Ctrl+Shift+P` → "Serial Monitor: Start Monitoring"
3. Seleccionar puerto COM del ESP32
4. Configurar baudrate: **115200**

### Formato de Logs para Plotter:
Los logs están formateados para graficar automáticamente:
```
POT: raw=1234 filtered=1200 digit=5
```

---

## 📈 Método 2: SimplySerial Plotter (Recomendado)

### Instalación:
```powershell
# Opción A: Chocolatey
choco install simplyserial

# Opción B: Descarga directa
# https://github.com/Phrellish/SimplySerial/releases
```

### Uso:
1. Compilar y flashear:
   ```powershell
   idf.py build flash
   ```

2. Iniciar SimplySerial con configuración:
   ```powershell
   simplyserial COM3 -baud:115200 -plot -config:plot_config.json
   ```

3. Mover el potenciómetro y observar:
   - **Línea Roja**: Señal RAW (con ruido)
   - **Línea Azul**: Señal Filtrada (suave)
   - **Línea Verde**: Dígito detectado (0-9)

---

## 📉 Método 3: Python + Matplotlib (Avanzado)

### Script de Visualización:
Crear archivo `plot_serial.py`:

```python
import serial
import matplotlib.pyplot as plt
import matplotlib.animation as animation
import re

# Configuración
PORT = 'COM3'  # Cambiar según tu puerto
BAUD = 115200
WINDOW = 100  # Muestras a mostrar

# Datos
raw_data = []
filtered_data = []
digit_data = []

# Serial
ser = serial.Serial(PORT, BAUD, timeout=1)

# Plot
fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 8))
fig.suptitle('Potenciómetro - Filtro IIR')

def animate(i):
    line = ser.readline().decode('utf-8', errors='ignore').strip()
    match = re.search(r'POT: raw=(\d+) filtered=(\d+) digit=(\d+)', line)
    
    if match:
        raw_data.append(int(match.group(1)))
        filtered_data.append(int(match.group(2)))
        digit_data.append(int(match.group(3)))
        
        # Limitar ventana
        if len(raw_data) > WINDOW:
            raw_data.pop(0)
            filtered_data.pop(0)
            digit_data.pop(0)
        
        # Graficar señales ADC
        ax1.clear()
        ax1.plot(raw_data, 'r-', label='RAW', alpha=0.6, linewidth=1)
        ax1.plot(filtered_data, 'b-', label='Filtrado IIR', linewidth=2)
        ax1.set_ylabel('Valor ADC')
        ax1.set_ylim(0, 4200)
        ax1.legend(loc='upper right')
        ax1.grid(True, alpha=0.3)
        
        # Graficar dígito
        ax2.clear()
        ax2.plot(digit_data, 'g-', label='Dígito', linewidth=2)
        ax2.set_ylabel('Dígito (0-9)')
        ax2.set_xlabel('Muestras')
        ax2.set_ylim(-0.5, 9.5)
        ax2.legend(loc='upper right')
        ax2.grid(True, alpha=0.3)

ani = animation.FuncAnimation(fig, animate, interval=50)
plt.tight_layout()
plt.show()

ser.close()
```

### Ejecutar:
```powershell
python plot_serial.py
```

---

## 🔧 Ajuste del Filtro

### Modificar Alpha en `main.c`:
```c
#define POT_FILTER_ALPHA  0.15f  // Cambiar este valor
```

| Alpha | Efecto | Uso |
|-------|--------|-----|
| **0.05** | Máximo suavizado | Señal muy ruidosa |
| **0.15** | Balance óptimo | Recomendado (default) |
| **0.30** | Respuesta rápida | Señal limpia |
| **0.50** | Mínimo filtrado | Solo picos extremos |

---

## 📝 Verificación del Filtro

### Logs Esperados:
```
I (1234) ACCESS: POT: raw=1205 filtered=1198 digit=5
I (1456) ACCESS: POT: raw=1210 filtered=1199 digit=5
I (1678) ACCESS: POT: raw=1198 filtered=1199 digit=5
I (1890) ACCESS: POT: raw=2340 filtered=1370 digit=6  <- Transición suave
I (2012) ACCESS: POT: raw=2355 filtered=1518 digit=6
```

Observa cómo `filtered` cambia más suavemente que `raw`.

---

## 🎛️ Prueba Rápida

1. Flashear código:
   ```powershell
   idf.py flash monitor
   ```

2. Mover potenciómetro **rápidamente** de un extremo a otro

3. Observar en monitor serial:
   - **RAW**: Saltará con valores erráticos
   - **Filtered**: Transición suave y estable
   - **Digit**: Cambiará cuando filtered se estabilice (2 segundos)

---

## 🚀 Extensiones VS Code Recomendadas

1. **Serial Monitor** (oficial Microsoft)
   - ID: `ms-vscode.vscode-serial-monitor`
   - Básico pero funcional

2. **SerialPlot** (para gráficas avanzadas)
   - Externo: https://github.com/hyOzd/serialplot

3. **Teleplot** (gráficas en navegador)
   - ID: `alexnesnes.teleplot`
   - Formato: `>raw:1234|filtered:1200|digit:5`

---

## 📚 Teoría del Filtro IIR

### Respuesta en Frecuencia:
- **Frecuencia de corte**: `fc = fs * α / (2π(1-α))`
- Con α=0.15 y fs=8.33Hz (120ms sample): **fc ≈ 0.2 Hz**
- Atenúa frecuencias > 0.2Hz (ruido de 60Hz del ADC)

### Constante de Tiempo:
- **τ = 1 / (α * fs) ≈ 0.8 segundos**
- Tiempo para alcanzar 63% del valor final

### Comparación FIR vs IIR:

| Aspecto | FIR | IIR (EMA) |
|---------|-----|-----------|
| **Memoria** | N valores | 1 valor ✅ |
| **Cómputo** | N multiplicaciones | 2 multiplicaciones ✅ |
| **Fase** | Lineal | No lineal |
| **Estabilidad** | Siempre estable ✅ | Puede oscilar |
| **Latencia** | N/2 muestras | Mínima ✅ |

Para este caso (potenciómetro lento), IIR es **óptimo**.

---

## 🐛 Troubleshooting

**Problema:** No veo cambios en filtered vs raw  
**Solución:** Aumentar `POT_FILTER_ALPHA` a 0.3

**Problema:** Filtrado demasiado lento  
**Solución:** Aumentar alpha a 0.25-0.30

**Problema:** No aparece en el plotter  
**Solución:** Verificar formato exacto del log: `POT: raw=X filtered=Y digit=Z`
