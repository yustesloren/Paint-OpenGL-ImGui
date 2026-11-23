# Paint con OpenGL

Aplicación tipo *Paint* desarrollada en C++ con OpenGL 3.3, GLFW, GLAD y Dear ImGui.
Permite dibujar sobre una grilla de celdas usando **algoritmos de rasterización propios**
(Bresenham, punto medio y relleno por línea de exploración) y aplicar **transformaciones
geométricas** con matrices 3×3 en coordenadas homogéneas.

| | |
|---|---|
| **Integrante** | Lorena Patricia Yustes Cifuentes |
| **Código** | 6014420 |
| **Curso** | Introducción a la Computación |
| **Profesor** | Fernando Javier Nossa Chaparro |

---

## Tabla de contenido
1. [Funcionalidades](#funcionalidades)
2. [Demostración](#demostración)
3. [Arquitectura del software](#arquitectura-del-software)
4. [Módulos y funciones](#módulos-y-funciones)
5. [Algoritmos implementados](#algoritmos-implementados)
6. [Estructura del repositorio](#estructura-del-repositorio)
7. [Dependencias](#dependencias)
8. [Compilación y ejecución](#compilación-y-ejecución)
9. [Uso del programa](#uso-del-programa)
10. [Limitaciones conocidas](#limitaciones-conocidas)
11. [Referencias](#referencias)

---

## Funcionalidades

| Funcionalidad | Descripción | Algoritmo / técnica |
|---|---|---|
| **Línea** | Línea entre dos clics | Algoritmo de **Bresenham** (8 octantes, solo aritmética entera) |
| **Círculo** | Centro + un punto del borde | Algoritmo del **punto medio** con simetría de 8 octantes |
| **Triángulo** | Tres clics (vértices) | Polígono cerrado: 3 lados con Bresenham |
| **Cuadrado** | Dos clics (esquinas opuestas) | 4 esquinas = **2 triángulos** (A,B,C) y (A,C,D) |
| **Forma libre** | Arrastrar el mouse | Polilínea abierta: cada punto unido al siguiente con Bresenham |
| **Relleno** | Triángulo, cuadrado y círculo rellenos | Relleno por **línea de exploración (scanline)** |
| **Color** | Cada figura conserva su propio color | Selector de color de Dear ImGui |
| **Transformaciones** | Traslación, rotación, escalamiento, reflexión y cizallamiento | **Matrices 3×3** en coordenadas homogéneas, con pivote en el centro |
| **Deshacer / Limpiar** | Quitar la última figura o todas | Lista de figuras redibujada cada frame |
| **Grilla** | Lienzo de celdas de 40 × 40 píxeles | Fragment shader con `discard` |

---

## Demostración

Video explicativo con demostración del software: *(https://youtu.be/JZOQlghxo88)*

---

## Arquitectura del software

### Organización general
Todo el programa está en `Main.cpp` y sigue los **6 momentos de un programa en OpenGL**
vistos en el curso:

| Momento | Qué se hace | Dónde |
|---|---|---|
| 1. Contexto | Ventana con GLFW, carga de OpenGL con GLAD, inicialización de Dear ImGui | Inicio de `main()` |
| 2. Geometría | Rectángulo de pantalla completa (grilla) y geometría de celdas generada cada frame | `main()`, `agregarCelda()` |
| 3. Shaders | Programa de la grilla y programa de las celdas | `crearProgramaShader()` |
| 4. Buffers | VAO/VBO de la grilla; **VAO + VBO + EBO** de las celdas | `main()` |
| 5. Render loop | Interfaz, rasterización y dibujo en cada frame | `while` de `main()` |
| 6. Liberación | Se liberan ImGui, buffers y programas | Final de `main()` |

### Idea central: figuras guardadas por puntos, no por celdas
Cada figura se guarda como una estructura `Figura` con:
- su **tipo** (línea, círculo, triángulo, cuadrado o libre),
- sus **puntos de control** originales (los clics que la definen),
- su **color**, si va **rellena**, y
- una **matriz de transformación acumulada** (empieza como identidad).

Las celdas pintadas **no se guardan**: se calculan en cada frame aplicando la matriz a los
puntos y rasterizando con el algoritmo correspondiente. Esto permite transformar figuras
sin perder calidad, deshacer, y reiniciar una figura a su forma original.

### Flujo de información: del clic a la pantalla
```
Clic del usuario
  └─ mouse_button_callback / cursor_position_callback  (GLFW)
       └─ mouseToGrid(): píxel → celda
            └─ crearFigura(): guarda tipo, puntos, color, relleno  →  lista "figuras"

Cada frame (render loop):
  para cada figura:
     puntosTransformados()  →  M · (x, y, 1) y redondeo
     rasterizarFigura()     →  Bresenham / punto medio / scanline  →  lista de celdas
     agregarCelda()         →  cada celda = 4 vértices + 6 índices (2 triángulos)
  glBufferData (VBO, EBO) → glDrawElements
     → vertex shader: celda → NDC   → fragment shader: color
```

### Capas de dibujo en cada frame
| Capa | Qué se dibuja | Cómo |
|---|---|---|
| 1. Fondo | Color oscuro | `glClear` |
| 2. Celdas | Todas las figuras con su color | `glDrawElements` con el VAO/VBO/EBO de celdas |
| 3. Grilla | Solo las rayas de la cuadrícula, encima de las celdas | Fragment shader que **descarta** (`discard`) los píxeles que no son raya |
| 4. Interfaz | Paneles de Dear ImGui | `ImGui_ImplOpenGL3_RenderDrawData` |

---

## Módulos y funciones

### Datos y estado
| Elemento | Responsabilidad |
|---|---|
| `Cell` | Una celda de la grilla (x, y enteros) |
| `Matriz3` | Matriz 3×3 para transformaciones en coordenadas homogéneas |
| `TipoFigura` | Tipos de figura: `LINEA`, `CIRCULO`, `TRIANGULO`, `CUADRADO`, `LIBRE` |
| `Figura` | Tipo, puntos originales, color, relleno y matriz acumulada |
| `figuras` | Lista de todas las figuras dibujadas |
| `colorActual`, `rellenoActivo`, `herramientaActual` | Opciones elegidas en la interfaz |
| `clicsPendientes`, `trazandoLibre` | Estado de la figura que se está construyendo |

### Rasterización (algoritmos propios)
| Función | Responsabilidad |
|---|---|
| `bresenhamLine()` | Línea de Bresenham en los 8 octantes |
| `midpointCircle()` | Contorno del círculo por punto medio y simetría de 8 octantes |
| `rasterizarPoligono()` | Polígono cerrado (triángulo, cuadrado) uniendo vértices con Bresenham |
| `rasterizarTrazo()` | Polilínea abierta (forma libre) |
| `rellenarScanline()` | Relleno de figuras convexas fila por fila |
| `esquinasCuadrado()` | Convierte 2 clics en las 4 esquinas de un cuadrado |
| `rasterizarFigura()` | Elige el algoritmo según el tipo de figura y aplica el relleno |

### Transformaciones
| Función | Responsabilidad |
|---|---|
| `matrizIdentidad()`, `multiplicar()`, `aplicarAPunto()` | Operaciones básicas con matrices |
| `matrizTraslacion()`, `matrizRotacion()`, `matrizEscala()`, `matrizReflexionX/Y()`, `matrizCizalla()` | Las 5 matrices de transformación |
| `aplicarTransformacion()` | Compone la transformación alrededor del centro: `T(+c)·X·T(−c)·M` |
| `puntosTransformados()` | Aplica la matriz acumulada a los puntos y redondea a celdas |

### OpenGL y entrada
| Función | Responsabilidad |
|---|---|
| `compilarShader()`, `crearProgramaShader()` | Compilan y enlazan shaders mostrando errores en consola |
| `agregarCelda()` | Convierte una celda en 4 vértices y 6 índices (2 triángulos) |
| `mouseToGrid()` | Convierte la posición del mouse (píxeles) en una celda |
| `mouse_button_callback()` | Clics: agrega puntos, crea figuras, inicia/termina trazos libres |
| `cursor_position_callback()` | Movimiento del mouse: agrega puntos al trazo libre |
| `framebuffer_size_callback()` | Ajusta el viewport al redimensionar la ventana |

### Interfaz (Dear ImGui)
| Función | Responsabilidad |
|---|---|
| `dibujarPanel()` | Ventana **Herramientas**: herramienta, color, relleno, deshacer, limpiar e información |
| `dibujarPanelTransformaciones()` | Ventana **Transformaciones**: lista de figuras y controles de las 5 transformaciones |

---

## Algoritmos implementados

### Bresenham para líneas
Usa un parámetro de decisión entero `P`, con valor inicial `P₀ = 2Δy − Δx`:
- Si `P < 0`: siguiente celda `(x+1, y)` y `P = P + 2Δy`.
- Si `P ≥ 0`: siguiente celda `(x+1, y+1)` y `P = P + 2Δy − 2Δx`.

Se generaliza a los 8 octantes con las direcciones `sx`, `sy` (±1) y el intercambio de
ejes cuando la línea es empinada (|m| > 1).

### Círculo por punto medio
Parámetro inicial `P₀ = 1 − R`. Se calculan los puntos del primer octante y se reflejan
en los otros 7 por simetría.

### Relleno por línea de exploración (scanline)
Sobre el contorno ya rasterizado, para cada fila se busca el `x` mínimo y el `x` máximo y
se pintan todas las celdas entre ellos. El cuadrado se rellena como dos triángulos.

### Transformaciones en coordenadas homogéneas
Cada punto `(x, y)` se trata como `(x, y, 1)` y se multiplica por una matriz 3×3.
Para que la figura gire o escale sobre sí misma se usa un pivote en su centro `c`:

```
M_nueva = T(+c) · X · T(−c) · M_actual
```

Los puntos originales nunca se modifican; la matriz se acumula y el redondeo a celdas se
hace una sola vez al dibujar, para no acumular error.

---

## Estructura del repositorio
```
Paint-OpenGL-ImGui/
├── Main.cpp                      Código fuente completo del programa
├── glad.c                        Cargador de funciones de OpenGL (GLAD)
├── Libraries/
│   ├── include/                  Cabeceras de GLFW, GLAD y KHR
│   ├── lib/glfw3.lib             Librería GLFW precompilada (x64, Debug)
│   └── imgui/                    Dear ImGui v1.93.0 (núcleo + backends GLFW y OpenGL3)
├── YoutubeOpenGL.slnx            Solución de Visual Studio
├── YoutubeOpenGL.vcxproj         Proyecto de Visual Studio (rutas y linker)
├── YoutubeOpenGL.vcxproj.filters Organización de archivos en Visual Studio
├── .gitignore                    Archivos de compilación que no se suben
└── README.md                     Este documento
```

---

## Dependencias
| Librería | Versión | Uso |
|---|---|---|
| OpenGL | 3.3 Core | API gráfica |
| GLFW | 3.4 | Ventana, contexto y eventos de mouse |
| GLAD | gl 3.3 core | Carga de funciones de OpenGL |
| Dear ImGui | 1.93.0 | Interfaz gráfica (backends `imgui_impl_glfw` e `imgui_impl_opengl3`) |

Todas las librerías están incluidas en la carpeta `Libraries/`: no hay que instalar nada adicional.

---

## Compilación y ejecución

**Requisitos:** Windows con **Visual Studio 2026** y la carga de trabajo *Desarrollo para el escritorio con C++*.

1. Clonar el repositorio:
   ```
   git clone https://github.com/yustesloren/Paint-OpenGL-ImGui.git
   ```
2. Abrir `YoutubeOpenGL.slnx` en Visual Studio.
3. Seleccionar la configuración **Debug** y la plataforma **x64**.
   (La librería `glfw3.lib` incluida está compilada para x64 en modo Debug.)
4. **Compilar → Recompilar solución**.
5. Ejecutar con **Depurar → Iniciar depuración** (F5).

### Configuración del proyecto (ya incluida en el `.vcxproj`)
| Propiedad | Valor |
|---|---|
| Directorios de archivos de inclusión | `$(ProjectDir)Libraries\include` y `$(ProjectDir)Libraries\imgui` |
| Directorios de bibliotecas | `$(ProjectDir)Libraries\lib` |
| Vinculador → Dependencias adicionales | `glfw3.lib; opengl32.lib` |
| Archivos fuente compilados | `Main.cpp`, `glad.c` y los 7 `.cpp` de Dear ImGui |

---

## Uso del programa

### Ventana "Herramientas"
- **Herramienta:** Línea, Círculo, Triángulo, Cuadrado o Libre.
- **Clic izquierdo:** coloca los puntos de la figura (aparecen en gris hasta completarla).
- **Clic derecho:** cancela la figura que se está construyendo.
- **Libre:** mantener presionado el clic izquierdo y arrastrar.
- **Color:** color de las figuras nuevas.
- **Rellenar figuras nuevas:** dibuja triángulos, cuadrados y círculos rellenos.
- **Deshacer / Limpiar todo.**

### Ventana "Transformaciones"
1. Seleccionar una figura en la lista.
2. Aplicar **Trasladar** (tx, ty), **Rotar** (grados), **Escalar** (sx, sy),
   **Espejo eje X / Y** o **Cizallar** (shx, shy).
3. **Reiniciar figura** devuelve la figura a su forma original.

---

## Limitaciones conocidas
- El relleno por línea de exploración está pensado para figuras **convexas**; por eso la forma libre no se rellena.
- El círculo se representa por centro y radio: con escalamiento no uniforme o cizallamiento sigue siendo un círculo (no se convierte en elipse).
- La librería GLFW incluida solo permite compilar en **Debug | x64**.

---

## Referencias
- Material del curso (Prof. Fernando J. Nossa Ch.): *Algoritmo de Bresenham para pintado de línea*, *Algoritmo de Bresenham para pintado de círculo*, *Matrices de transformación*, *Proyecciones*, *OpenGL Scheme* y *6 Important Moments in Client Main OpenGL*.
- freeCodeCamp, Victor Gordan: *OpenGL Course – Create 3D and 2D Graphics With C++*.
- Dear ImGui: https://github.com/ocornut/imgui
