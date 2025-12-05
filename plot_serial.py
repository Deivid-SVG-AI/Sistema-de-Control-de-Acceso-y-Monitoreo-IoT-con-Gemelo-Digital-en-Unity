#!/usr/bin/env python3
"""
Script de visualización en tiempo real para el potenciómetro filtrado
Uso: python plot_serial.py COM3
"""
import serial
import matplotlib.pyplot as plt
import matplotlib.animation as animation
import re
import sys

# Configuración
PORT = sys.argv[1] if len(sys.argv) > 1 else 'COM3'
BAUD = 115200
WINDOW = 100  # Muestras a mostrar

# Datos
raw_data = []
filtered_data = []
digit_data = []
time_data = []

try:
    ser = serial.Serial(PORT, BAUD, timeout=1)
    print(f"✓ Conectado a {PORT} @ {BAUD} baud")
except Exception as e:
    print(f"✗ Error: {e}")
    sys.exit(1)

# Configuración de figura
fig = plt.figure(figsize=(14, 8))
gs = fig.add_gridspec(3, 1, hspace=0.3)
ax1 = fig.add_subplot(gs[0:2, 0])  # Señales ADC (2/3 del espacio)
ax2 = fig.add_subplot(gs[2, 0])     # Dígito (1/3 del espacio)

fig.suptitle('Filtro IIR (EMA) - Potenciómetro ADC', fontsize=14, fontweight='bold')

sample_count = 0

def animate(i):
    global sample_count
    
    try:
        line = ser.readline().decode('utf-8', errors='ignore').strip()
        match = re.search(r'POT: raw=(\d+) filtered=(\d+) digit=(\d+)', line)
        
        if match:
            raw = int(match.group(1))
            filtered = int(match.group(2))
            digit = int(match.group(3))
            
            raw_data.append(raw)
            filtered_data.append(filtered)
            digit_data.append(digit)
            time_data.append(sample_count)
            sample_count += 1
            
            # Limitar ventana
            if len(raw_data) > WINDOW:
                raw_data.pop(0)
                filtered_data.pop(0)
                digit_data.pop(0)
                time_data.pop(0)
            
            # Graficar señales ADC
            ax1.clear()
            ax1.plot(time_data, raw_data, 'r-', label='RAW (con ruido)', 
                    alpha=0.5, linewidth=1.5, marker='o', markersize=3, markevery=5)
            ax1.plot(time_data, filtered_data, 'b-', label='Filtrado IIR (α=0.15)', 
                    linewidth=2.5)
            ax1.set_ylabel('Valor ADC', fontsize=11)
            ax1.set_ylim(-50, 4200)
            ax1.legend(loc='upper right', fontsize=10)
            ax1.grid(True, alpha=0.3, linestyle='--')
            ax1.set_title(f'Señal ADC - Muestras: {len(raw_data)}', fontsize=11)
            
            # Graficar dígito
            ax2.clear()
            ax2.plot(time_data, digit_data, 'g-', label='Dígito Detectado', 
                    linewidth=2.5, marker='s', markersize=5)
            ax2.set_ylabel('Dígito', fontsize=11)
            ax2.set_xlabel('Muestra', fontsize=11)
            ax2.set_ylim(-0.5, 9.5)
            ax2.set_yticks(range(10))
            ax2.legend(loc='upper right', fontsize=10)
            ax2.grid(True, alpha=0.3, linestyle='--')
            
            # Estadísticas en tiempo real
            if len(raw_data) > 1:
                noise = abs(raw - filtered)
                ax1.text(0.02, 0.95, f'Ruido actual: {noise}', 
                        transform=ax1.transAxes, fontsize=9, 
                        bbox=dict(boxstyle='round', facecolor='wheat', alpha=0.5))
    
    except KeyboardInterrupt:
        ser.close()
        plt.close()
        sys.exit(0)

# Animación
ani = animation.FuncAnimation(fig, animate, interval=50, cache_frame_data=False)
plt.tight_layout()

print("\n📊 Graficando en tiempo real...")
print("🔧 Mueve el potenciómetro para ver el efecto del filtro")
print("❌ Ctrl+C para detener\n")

try:
    plt.show()
except KeyboardInterrupt:
    print("\n✓ Visualización detenida")
finally:
    ser.close()
