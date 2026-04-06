# Pong ESP32 en matriz LED 8x8 (HL-M2388BRG) - PlatformIO / VSCODE
Proyecto de videojuego tipo **Pong** implementado en una **ESP32** y mostrado en una **matriz LED 8x8 bicolor HL-M2388BRG**, usando las columnas 0 y 7 de color orjo y 1 a 6 de color verde.
El displays se controla mediante **multiplexeado por filas con ISR de timer**, y el juego incluye modo **Jugador vs. Jugador** o **Jugador vs. CPU** seleccionable ddesde un menú visual.

---

## Caracteristicas
- **Multiplexeado estable**: refresco por filas usando un timer ISR para evitar parpadeos y ghosting en la matriz de LEDs.
- **Menu sin texto (4s)**: una barra en la paleta derecha con un timer de 4s decide el modo: 
    - Si J2 presiona durante la cuenta -> **J1 vs. J2**.
    - Si no presiona y termina la cuenta -> **J1 vs. CPU**.
- **CPU "justa"**: no es perfecta (su reaccion es por ticks y con un pequeño error para que no sea injusta con el jugador).
- **Serve con animación**: al perder, la pelota vuelve al centro y parpadea (y tambien parpadea la paleta del que perdio).
- **Rebote con habilidad**: el ángulo de salida depende de donde golpee la pelota en la paleta (arriba/centro/abajo).
- **Velocidad progresiva controlada**: la pelota inicia lenta y acelera gradualmente (configurable).

---

## Controles

### Jugador 1 (paleta izquierda)
- **BTN_J1_UP**: sube paleta.
- **BTN_J!_DOWN**: baja paleta.

### Jugador 2 (paleta derecha)
- **BTN_J2_UP**: sube paleta (solo en modo VS_J2).
- **BTN_J2_DOWN**: baja paleta (solo en modo VS_J2).
- En el **MENU**, cualquier boton de J2 seleciona **VS_J2** y comienza el juego.

### Reset
- Mantener presipnado cualquier botón por ~2s reinicia al menú (configurable).

---

## Mecánicas del juego
- Las paletas miden **3 LEDs** de alto.
- La pelota rebota en paredes superior e inferior.
- Al chocar con paleta:
    - golpe arriba -> sale hacia arriba.
    - golpe centro -> sale recta.
    - golpe abajo -> sale hacia abajo.
- Si un jugador falla:
    - Se activa el **serve**: pelota centrada + parpadeo corto.
    - **La paleta del perdedor parpadea** para que se note que fue "punto" y no un bug.
    - La pelota sale hacia el jugador que perdio (serve "hacia el perdedor").

---

## Hardware usado
- **ESP32** (3.3V lógica)
- **Matriz 8x8 bicolor HL-M2388BRG**
- **Transistores PNP(2N3906)** para filas (high-side)
- **Transistores NPN(2N3904)** para columnas (low-side)
- Resistencias:
    - **Base PNP**: 1kΩ.
    - **Base NPN**: 1kΩ.
    - **Limitación LED**: 330Ω por columna (depende del brillo).
    - **Pull-up botones**: 10kΩ.
- Botones con pull-up externo.

---

## Conexión del hardware
Este proyecto usa la topología para este tipo de matriz:

- **Filas (ROW)**: se alimentan desde arriba (high-side) -> PNP 3906.
- **Columnas (COL)**: se hunden a tierra (low-side) -> NPN 3904.

### Lógica resultante
- **Fila ON** (PNP conduce): GPIO fila = LOW.
- **Fila OFF**: GPIO de fila = HIGH.
- **Columna ON** (NPN conduce a GND): Gpio de columna = HIGH.
- **Columna OFF**: GPIO de columna = LOW.

### 1) Filas (ROW1, ..., ROW8) con PNP 2N3906 (high-side)
Para cada fila:

- **Emisor (E) PNP -> 3.3V**.
- **Colector (C) PNP -> Pin ROWx de la matriz**.
- **Base (B) PNP -> GPIO_ROWx por resistencia de un 1kΩ**.

Esquema conceptual:
3.3V -> Emisor  Colector -> ROWx  Base -> 1kΩ -> GPIO_ROWx.

### Columnas (COL1, ..., COL8) con NPN (low-side)
Para cada columna del color que uses:

- **Pin COLx -> Resistencia LED (330Ω) -> Colector**
- **Emisor -> GND**
- **Base NPN -> GPIO_COLx por resistencia 1kΩ**

Esquema conceptual:
3.3V - > Colector  Emisor -> GND  Base -> 1kΩ -> GPIO_COLx

### Botones
Se recomienda pull-up externo de 10kΩ a 3.3V y boton a GND.
- **Presionado = 0 (low)**
- **Suelto = 1 (high)**

## Pinout utilizado (configurable)

### Filas 
static const gpio_num_t ROW_PINS[8] = {13, 16, 17, 18, 19, 21, 22, 23};

### Columnas
static const gpio_num_t COL_PINS[8] = {25, 26, 27, 32, 33, 14, 5, 4};

### Botones

#define BTN_J1_UP    34
#define BTN_J1_DOWN  35
#define BTN_J2_UP    36
#define BTN_J2_DOWN  39
