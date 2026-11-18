#include <glad/glad.h>
#include <GLFW/glfw3.h>

// Dear ImGui: núcleo + adaptador de entrada (GLFW) + adaptador de dibujo (OpenGL 3)
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <string>      


// CONFIGURACIÓN DE LA VENTANA Y CUADRÍCULA


const int WINDOW_WIDTH = 800;   // Ancho de la ventana en píxeles
const int WINDOW_HEIGHT = 800;  // Alto de la ventana en píxeles

const float CELL_SIZE = 40.0f;  // Tamaño en píxeles de cada celda de la cuadrícula

int windowWidth = WINDOW_WIDTH;
int windowHeight = WINDOW_HEIGHT;

// ESTRUCTURAS DE DATOS

// Representa una celda discreta (coordenadas enteras en la cuadrícula)
struct Cell
{
    int x;
    int y;
};

// ============================================================================
// MATRIZ 3x3 PARA TRANSFORMACIONES 2D (diapositiva "Matrices de transformación")
// Se usan COORDENADAS HOMOGÉNEAS: el punto (x, y) se trata como (x, y, 1).
// Así TODAS las transformaciones, incluida la traslación, son una matriz 3x3:
//        punto nuevo = M · (x, y, 1)
// m[fila][columna]
// ============================================================================
struct Matriz3
{
    float m[3][3];
};

// Identidad: "no transformar nada". M · p = p
Matriz3 matrizIdentidad()
{
    return { { { 1.0f, 0.0f, 0.0f },
               { 0.0f, 1.0f, 0.0f },
               { 0.0f, 0.0f, 1.0f } } };
}

// COMPOSICIÓN: el producto A · B equivale a "aplicar primero B y luego A".
// Cada casilla R[i][j] = fila i de A  por  columna j de B.
Matriz3 multiplicar(const Matriz3& A, const Matriz3& B)
{
    Matriz3 R;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
        {
            R.m[i][j] = 0.0f;
            for (int k = 0; k < 3; k++)
                R.m[i][j] += A.m[i][k] * B.m[k][j];
        }
    return R;
}

// Aplica la matriz al punto (x, y, 1). Solo se calculan x' e y':
// la tercera fila siempre da 1 en transformaciones afines.
void aplicarAPunto(const Matriz3& M, float x, float y, float& xNuevo, float& yNuevo)
{
    xNuevo = M.m[0][0] * x + M.m[0][1] * y + M.m[0][2] * 1.0f;
    yNuevo = M.m[1][0] * x + M.m[1][1] * y + M.m[1][2] * 1.0f;
}

// ---------------- Las 5 transformaciones de la diapositiva ----------------

// TRASLACIÓN: x' = x + tx,  y' = y + ty
Matriz3 matrizTraslacion(float tx, float ty)
{
    return { { { 1.0f, 0.0f, tx   },
               { 0.0f, 1.0f, ty   },
               { 0.0f, 0.0f, 1.0f } } };
}

// ROTACIÓN (alrededor del origen, sentido antihorario):
//   x' = x·cos θ − y·sin θ,   y' = x·sin θ + y·cos θ
Matriz3 matrizRotacion(float grados)
{
    float rad = grados * 3.14159265f / 180.0f;   // las funciones cos/sin usan radianes
    float c = std::cos(rad);
    float s = std::sin(rad);
    return { { { c,    -s,    0.0f },
               { s,     c,    0.0f },
               { 0.0f,  0.0f, 1.0f } } };
}

// ESCALAMIENTO: x' = sx·x,  y' = sy·y   (>1 agranda, <1 achica)
Matriz3 matrizEscala(float sx, float sy)
{
    return { { { sx,   0.0f, 0.0f },
               { 0.0f, sy,   0.0f },
               { 0.0f, 0.0f, 1.0f } } };
}

// REFLEXIÓN respecto al eje X: voltea arriba/abajo  (x' = x,  y' = −y)
Matriz3 matrizReflexionX()
{
    return { { {  1.0f,  0.0f, 0.0f },
               {  0.0f, -1.0f, 0.0f },
               {  0.0f,  0.0f, 1.0f } } };
}

// REFLEXIÓN respecto al eje Y: voltea izquierda/derecha  (x' = −x,  y' = y)
Matriz3 matrizReflexionY()
{
    return { { { -1.0f,  0.0f, 0.0f },
               {  0.0f,  1.0f, 0.0f },
               {  0.0f,  0.0f, 1.0f } } };
}

// CIZALLAMIENTO: x' = x + shx·y,  y' = y + shy·x
Matriz3 matrizCizalla(float shx, float shy)
{
    return { { { 1.0f, shx,  0.0f },
               { shy,  1.0f, 0.0f },
               { 0.0f, 0.0f, 1.0f } } };
}

// Tipos de figura que sabe dibujar el programa.

enum TipoFigura { LINEA, CIRCULO, TRIANGULO, CUADRADO, LIBRE };

// Una figura se guarda por sus PUNTOS DE CONTROL (los clics que la definen),
// NO por sus celdas pintadas. Las celdas se calculan cada frame con el algoritmo.
//   LINEA     -> puntos[0] = inicio,  puntos[1] = fin
//   CIRCULO   -> puntos[0] = centro,  puntos[1] = un punto del borde (define el radio)
//   TRIANGULO -> puntos[0..2] = los 3 vértices (A, B, C)
//   CUADRADO  -> puntos[0..3] = las 4 esquinas (A, B, C, D)
//                = triángulo (A, B, C) + triángulo (A, C, D)
//   LIBRE     -> puntos[0..n] = las celdas por donde pasó el mouse (trazo a mano alzada)
struct Figura
{
    TipoFigura tipo = LINEA;                  // valor inicial por defecto (evita "basura")
    std::vector<Cell> puntos;                 // un vector siempre empieza vacío
    float color[3] = { 1.0f, 1.0f, 1.0f };    // r, g, b entre 0.0 y 1.0 (blanco por defecto)
    bool relleno = false;                     // true: se pinta el interior (scanline), no solo el borde
    Matriz3 transformacion = matrizIdentidad();   // transformaciones acumuladas (empieza "sin transformar")

};


// Lista ÚNICA con todas las figuras, en el orden en que se dibujaron.
// Si dos figuras se cruzan, la última dibujada queda encima (como en Paint).
std::vector<Figura> figuras;

// Color con el que se crearán las figuras nuevas.
// Lo modifica el selector de color del panel (ImGui::ColorEdit3 en dibujarPanel).
float colorActual[3] = { 1.0f, 0.20f, 0.10f };

// VARIABLES DE ESTADO DE LA INTERFAZ Y DEL MOUSE

// Herramienta seleccionada en el panel de ImGui (el clic izquierdo dibuja con ella)
TipoFigura herramientaActual = LINEA;

// Clics que el usuario ya hizo para la figura que está construyendo.
// Cuando hay tantos como necesita la herramienta, se crea la figura y se vacía.
std::vector<Cell> clicsPendientes;


// true mientras el usuario mantiene presionado el clic con la herramienta LIBRE.
// Entre PRESIONAR y SOLTAR, cada movimiento del mouse agrega puntos al trazo.
bool trazandoLibre = false;
// Opción "Rellenar" del panel: las figuras NUEVAS se crearán rellenas si está activa.
// (Igual que colorActual: se copia a cada figura al crearla.)
bool rellenoActivo = false;

// Índice de la figura que se va a transformar (en la lista 'figuras').
// -1 significa "ninguna seleccionada". Se elige en la ventana "Transformaciones".
int figuraSeleccionada = -1;

// CALLBACK DE REDIMENSIONAMIENTO DE VENTANA


void framebuffer_size_callback(
    GLFWwindow* window,
    int width,
    int height)
{
    glViewport(0, 0, width, height); // Actualiza el área de dibujo de OpenGL
    windowWidth = width;
    windowHeight = height;
}


// CONVERSIÓN DE COORDENADAS DE PANTALLA (MOUSE) A CELDA


Cell mouseToGrid(double xpos, double ypos)
{
    // Divide la posición horizontal del mouse entre el tamaño de la celda para obtener la columna
    int gridX = (int)(xpos / CELL_SIZE);

    // Invierte el eje Y (la pantalla empieza arriba, OpenGL abajo) y calcula la fila
    int gridY = (int)((windowHeight - ypos) / CELL_SIZE);

    return { gridX, gridY };
}

// ============================================================================
// ALGORITMO DE BRESENHAM PARA LÍNEA (diapositiva 13 del curso)
// Decide qué celdas pintar usando SOLO aritmética entera, con un parámetro de
// decisión P. Generalizado a los 8 octantes con:
//   - sx, sy: dirección del avance (+1 o -1)
//   - intercambio de ejes cuando la línea es empinada (|m| > 1)
// ============================================================================
std::vector<Cell> bresenhamLine(int x0, int y0, int x1, int y1)
{
    std::vector<Cell> puntos;

    // Δx y Δy en valor absoluto: las reglas de la diapositiva usan distancias positivas
    int dx = std::abs(x1 - x0);
    int dy = std::abs(y1 - y0);

    // Dirección del avance en cada eje: +1 (derecha/arriba) o -1 (izquierda/abajo)
    int sx = (x1 >= x0) ? 1 : -1;
    int sy = (y1 >= y0) ? 1 : -1;

    // Si la línea es empinada (|m| > 1), se intercambian los papeles de los ejes:
    // 'dx' pasa a ser la distancia del eje que avanza siempre (el eje dominante).
    bool empinada = dy > dx;
    if (empinada)
        std::swap(dx, dy);

    // Parámetro de decisión inicial: P0 = 2Δy - Δx
    int P = 2 * dy - dx;

    int x = x0;
    int y = y0;

    // Se pintan Δx + 1 celdas (incluye el punto inicial y el final)
    for (int i = 0; i <= dx; i++)
    {
        puntos.push_back({ x, y });   // pintar la celda actual

        if (P >= 0)
        {
            // P >= 0: la recta está más cerca de la celda "de arriba"
            // -> también se avanza en el eje secundario
            if (empinada) x += sx;
            else          y += sy;
            P = P + 2 * dy - 2 * dx;  // P(k+1) = P(k) + 2Δy - 2Δx
        }
        else
        {
            // P < 0: la recta está más cerca de la celda "de abajo"
            // -> solo se avanza en el eje dominante
            P = P + 2 * dy;           // P(k+1) = P(k) + 2Δy
        }

        // Siempre se avanza una celda en el eje dominante (x + 1 en la diapositiva)
        if (empinada) y += sy;
        else          x += sx;
    }

    return puntos;
}

// EL CÍRCULO DE PUNTO MEDIO (OCTANTES Y PARÁMETRO DE DECISIÓN)


std::vector<Cell> midpointCircle(
    int cx,
    int cy,
    int radius)
{
    std::vector<Cell> puntos;

    int x = 0;
    int y = radius;

    // PARÁMETRO DE DECISIÓN (p):
    // Inicia como un número entero basado en el radio (`1 - radius`). 
    // Funciona como un "semáforo" matemático para decidir qué píxel encender 
    // en cada paso sin necesidad de usar números decimales o flotantes en el bucle.
    int p = 1 - radius;

    while (x <= y)
    {
        // LAS 8 OCTANTES:
        // Una circunferencia es perfectamente simétrica en 8 secciones de 45 grados. 
        // Se calcula únicamente los puntos del primer octante y los refleja 
        // automáticamente hacia los otros 7 octantes sumando o restando respecto al centro (cx, cy).
        puntos.push_back({ cx + x, cy + y }); // 1er Octante
        puntos.push_back({ cx - x, cy + y }); // 2do Octante
        puntos.push_back({ cx + x, cy - y }); // 3er Octante
        puntos.push_back({ cx - x, cy - y }); // 4to Octante
        puntos.push_back({ cx + y, cy + x }); // 5to Octante
        puntos.push_back({ cx - y, cy + x }); // 6to Octante
        puntos.push_back({ cx + y, cy - x }); // 7mo Octante
        puntos.push_back({ cx - y, cy - x }); // 8vo Octante

        x++;

        // EVALUACIÓN DEL PARÁMETRO DE DECISIÓN:
        if (p < 0)
        {
            // Si p < 0, el punto medio está dentro del círculo; avanzamos solo en X
            p = p + 2 * x + 1;
        }
        else
        {
            // Si p >= 0, el punto medio está fuera; disminuimos Y y avanzamos en X
            y--;
            p = p + 2 * (x - y) + 1;
        }
    }

    return puntos;
}

// ============================================================================
// RASTERIZAR UN POLÍGONO: une cada vértice con el siguiente usando Bresenham,
// y el último con el primero para cerrar la figura.
// Sirve para el triángulo (3 vértices) y el cuadrado (4 vértices).
// ============================================================================
std::vector<Cell> rasterizarPoligono(const std::vector<Cell>& vertices)
{
    std::vector<Cell> celdas;
    int n = (int)vertices.size();

    for (int i = 0; i < n; i++)
    {
        Cell a = vertices[i];
        Cell b = vertices[(i + 1) % n];   // % n: después del último vuelve al primero

        // Cada lado del polígono es una línea de Bresenham
        std::vector<Cell> lado = bresenhamLine(a.x, a.y, b.x, b.y);

        // Agrega todas las celdas de este lado al final de la lista
        celdas.insert(celdas.end(), lado.begin(), lado.end());
    }
    return celdas;
}

// ============================================================================
// RASTERIZAR UN TRAZO LIBRE (polilínea ABIERTA): une cada punto con el
// siguiente usando Bresenham. A diferencia del polígono, el último punto
// NO se une con el primero. Así el trazo queda continuo aunque el mouse
// se haya movido rápido y se haya "saltado" celdas.
// ============================================================================
std::vector<Cell> rasterizarTrazo(const std::vector<Cell>& puntos)
{
    std::vector<Cell> celdas;

    // Un trazo de un solo punto (el usuario hizo clic sin mover): una celda
    if (puntos.size() == 1)
    {
        celdas.push_back(puntos[0]);
        return celdas;
    }

    // Une el punto i con el i+1. El ciclo termina en el PENÚLTIMO punto
    // (i + 1 < tamaño), así nunca se busca un punto que no existe y
    // el trazo queda abierto.
    for (size_t i = 0; i + 1 < puntos.size(); i++)
    {
        std::vector<Cell> tramo = bresenhamLine(puntos[i].x, puntos[i].y,
            puntos[i + 1].x, puntos[i + 1].y);
        celdas.insert(celdas.end(), tramo.begin(), tramo.end());
    }
    return celdas;
}

// ============================================================================
// RELLENO POR LÍNEA DE EXPLORACIÓN (SCANLINE) - diapositiva 8 del curso.
// Recibe el CONTORNO de una figura convexa (ya rasterizado con Bresenham o
// punto medio) y la rellena FILA POR FILA:
//   1) Para cada fila y: buscar el x mínimo y el x máximo del contorno.
//   2) Pintar todas las celdas entre esos dos bordes.
// Funciona para figuras CONVEXAS (triángulo, cuadrado, círculo).
// ============================================================================
std::vector<Cell> rellenarScanline(const std::vector<Cell>& contorno)
{
    std::vector<Cell> celdas;
    if (contorno.empty()) return celdas;

    // --- 1) Rango de filas que ocupa la figura: desde yMin hasta yMax ---
    int yMin = contorno[0].y;
    int yMax = contorno[0].y;
    for (const Cell& c : contorno)
    {
        yMin = std::min(yMin, c.y);
        yMax = std::max(yMax, c.y);
    }
    int filas = yMax - yMin + 1;

    // --- 2) Para cada fila, el borde izquierdo (xMin) y el derecho (xMax) ---
    // Se empieza con valores "imposibles" para que el primer x real los reemplace.
    const int MUY_GRANDE = 1000000;
    std::vector<int> xMin(filas, MUY_GRANDE);
    std::vector<int> xMax(filas, -MUY_GRANDE);

    for (const Cell& c : contorno)
    {
        int f = c.y - yMin;                  // número de fila (0 = la de más abajo)
        xMin[f] = std::min(xMin[f], c.x);    // ¿esta celda está más a la izquierda?
        xMax[f] = std::max(xMax[f], c.x);    // ¿esta celda está más a la derecha?
    }

    // --- 3) Pintar cada fila de borde a borde ---
    for (int f = 0; f < filas; f++)
    {
        if (xMin[f] > xMax[f]) continue;     // fila sin celdas de contorno: se salta

        for (int x = xMin[f]; x <= xMax[f]; x++)
            celdas.push_back({ x, yMin + f });
    }
    return celdas;
}

// ============================================================================
// ESQUINAS DE UN CUADRADO a partir de 2 clics (esquinas opuestas).
// El lado es el MAYOR entre |dx| y |dy| para que sea cuadrado (no rectángulo).
// Orden A, B, C, D: así el cuadrado = triángulo (A,B,C) + triángulo (A,C,D).
// ============================================================================
std::vector<Cell> esquinasCuadrado(Cell a, Cell b)
{
    int dx = b.x - a.x;
    int dy = b.y - a.y;

    int lado = std::max(std::abs(dx), std::abs(dy));   // lado del cuadrado

    // Dirección en que crece el cuadrado (igual que sx, sy en Bresenham)
    int sx = (dx >= 0) ? 1 : -1;
    int sy = (dy >= 0) ? 1 : -1;

    return {
        { a.x,             a.y             },   // A: el primer clic
        { a.x + sx * lado, a.y             },   // B: misma fila que A
        { a.x + sx * lado, a.y + sy * lado },   // C: esquina opuesta a A
        { a.x,             a.y + sy * lado }    // D: misma columna que A
    };
}

// ============================================================================
// PUNTOS TRANSFORMADOS: aplica la matriz acumulada de la figura a cada uno de
// sus puntos ORIGINALES y redondea a la celda más cercana.
// Se redondea UNA sola vez, al dibujar, para que el error no se acumule.
// ============================================================================
std::vector<Cell> puntosTransformados(const Figura& figura)
{
    std::vector<Cell> resultado;
    for (const Cell& c : figura.puntos)
    {
        float xNuevo, yNuevo;
        // punto nuevo = M · (x, y, 1)
        aplicarAPunto(figura.transformacion, (float)c.x, (float)c.y, xNuevo, yNuevo);

        // La grilla solo tiene celdas enteras: se redondea al entero más cercano
        resultado.push_back({ (int)std::round(xNuevo), (int)std::round(yNuevo) });
    }
    return resultado;
}

// ============================================================================
// RASTERIZAR UNA FIGURA: convierte sus puntos de control en la lista de celdas
// que hay que pintar, usando el algoritmo que corresponde a su tipo.
// ============================================================================

std::vector<Cell> rasterizarFigura(const Figura& figura)
{
    // Primero se aplican las transformaciones (matriz acumulada) a los puntos
    // ORIGINALES. Desde aquí se trabaja con 'pts' en lugar de figura.puntos.
    std::vector<Cell> pts = puntosTransformados(figura);

    if (figura.tipo == LINEA)
    {
        // Algoritmo de Bresenham (diapositiva 13): solo aritmética entera
        return bresenhamLine(pts[0].x, pts[0].y, pts[1].x, pts[1].y);
    }

    if (figura.tipo == CIRCULO)
    {
        Cell centro = pts[0];
        Cell borde = pts[1];

        // Radio = distancia euclidiana entre el centro y el punto del borde
        // (ya transformados: si se escaló, el radio cambia)
        int dx = borde.x - centro.x;
        int dy = borde.y - centro.y;
        int radio = (int)std::sqrt((float)(dx * dx + dy * dy));
        if (radio < 1) radio = 1;

        std::vector<Cell> contorno = midpointCircle(centro.x, centro.y, radio);

        // Relleno: cada fila de un punto simétrico del círculo al otro
        if (figura.relleno)
            return rellenarScanline(contorno);
        return contorno;
    }

    if (figura.tipo == TRIANGULO)
    {
        std::vector<Cell> contorno = rasterizarPoligono(pts);

        // Relleno scanline sobre el contorno de Bresenham
        if (figura.relleno)
            return rellenarScanline(contorno);
        return contorno;
    }

    if (figura.tipo == CUADRADO)
    {
        if (figura.relleno)
        {
            // CUADRADO CONSTRUIDO A PARTIR DE TRIÁNGULOS:
            // se rellena el triángulo (A, B, C) y el triángulo (A, C, D)
            // pts[0]=A, pts[1]=B, pts[2]=C, pts[3]=D
            std::vector<Cell> triangulo1 = rellenarScanline(rasterizarPoligono({ pts[0], pts[1], pts[2] }));
            std::vector<Cell> triangulo2 = rellenarScanline(rasterizarPoligono({ pts[0], pts[2], pts[3] }));

            // Se unen las celdas de los dos triángulos en una sola lista
            triangulo1.insert(triangulo1.end(), triangulo2.begin(), triangulo2.end());
            return triangulo1;
        }
        // Sin relleno: solo los 4 lados
        return rasterizarPoligono(pts);
    }

    if (figura.tipo == LIBRE)
    {
        // Trazo a mano alzada: polilínea abierta unida con Bresenham.
        // No se rellena: puede tener cualquier forma (no necesariamente convexa).
        return rasterizarTrazo(pts);
    }

    return {};   // tipo desconocido: ninguna celda
}

// ============================================================================
// CREAR UNA FIGURA nueva con el color que esté seleccionado en este momento.
// ============================================================================
void crearFigura(TipoFigura tipo, const std::vector<Cell>& puntos)
{
    Figura nueva;
    nueva.tipo = tipo;
    nueva.puntos = puntos;

    // El cuadrado se crea con 2 clics, pero se GUARDA con sus 4 esquinas
    // (así las transformaciones de la fase 8 pueden rotar cada esquina)
    if (tipo == CUADRADO)

        nueva.puntos = esquinasCuadrado(puntos[0], puntos[1]);

    // Se COPIA el color actual: si luego el usuario cambia de color,
    // esta figura conserva el suyo.
    for (int i = 0; i < 3; i++)
        nueva.color[i] = colorActual[i];

    // Se COPIA también la opción "Rellenar" del panel
    nueva.relleno = rellenoActivo;

    figuras.push_back(nueva);
}


// Cuántos clics necesita cada herramienta para crear su figura.
// Cuando clicsPendientes llega a este número, se crea la figura.

int clicsNecesarios(TipoFigura tipo)
{
    switch (tipo)
    {
    case LINEA:     return 2;   // inicio y fin
    case CIRCULO:   return 2;   // centro y un punto del borde
    case TRIANGULO: return 3;   // los 3 vértices
    case CUADRADO:  return 2;   // dos esquinas opuestas (se convierten en 4 esquinas)
    }
    return 2;                   // por seguridad, si llegara un tipo desconocido
}

// ============================================================================
// APLICAR UNA TRANSFORMACIÓN a la figura número 'indice'.
// La transformación X se hace ALREDEDOR DEL CENTRO de la figura (pivote):
//     M_nueva = T(+c) · X · T(−c) · M_actual
// Se lee de derecha a izquierda: lo que la figura ya tenía, llevar el centro
// al origen, transformar, devolver el centro a su lugar.
// ============================================================================
void aplicarTransformacion(int indice, const Matriz3& X)
{
    // Seguridad: el índice debe corresponder a una figura que exista
    if (indice < 0 || indice >= (int)figuras.size()) return;

    Figura& f = figuras[indice];   // '&': se modifica la figura REAL, no una copia

    // --- 1) Centro ACTUAL de la figura (con las transformaciones que ya tiene) ---
    float cx = 0.0f, cy = 0.0f;
    if (f.tipo == CIRCULO)
    {
        // El círculo gira y escala alrededor de SU centro (puntos[0])
        aplicarAPunto(f.transformacion, (float)f.puntos[0].x, (float)f.puntos[0].y, cx, cy);
    }
    else
    {
        // Las demás figuras: centroide = promedio de sus puntos transformados
        for (const Cell& c : f.puntos)
        {
            float xn, yn;
            aplicarAPunto(f.transformacion, (float)c.x, (float)c.y, xn, yn);
            cx += xn;
            cy += yn;
        }
        cx /= (float)f.puntos.size();
        cy /= (float)f.puntos.size();
    }

    // --- 2) Composición con pivote: T(+c) · X · T(−c) ---
    Matriz3 alrededorDelCentro =
        multiplicar(matrizTraslacion(cx, cy),
            multiplicar(X, matrizTraslacion(-cx, -cy)));

    // --- 3) Acumular: la nueva transformación se aplica DESPUÉS de las anteriores ---
    f.transformacion = multiplicar(alrededorDelCentro, f.transformacion);
}

// SHADERS DE OPENGL (VERTEX Y FRAGMENT SHADER)


const char* vertexShaderSource =
"#version 330 core\n"
"layout (location = 0) in vec3 aPos;\n"
"void main()\n"
"{\n"
"    gl_Position = vec4(aPos, 1.0);\n"
"}\n";

// FRAGMENT SHADER DE LA GRILLA: ahora SOLO dibuja las rayas de la grilla.
// Los píxeles que no son raya se DESCARTAN para que se vea lo que hay debajo
// (el fondo o las celdas pintadas).
const char* fragmentShaderSource =
"#version 330 core\n"
"out vec4 FragColor;\n"
"uniform float uCellSize;\n"
"void main()\n"
"{\n"
"    vec2 insideCell = mod(gl_FragCoord.xy, uCellSize);\n"
"    float gridWidth = 1.0;\n"
"    if (insideCell.x >= gridWidth && insideCell.y >= gridWidth)\n"
"        discard;\n"
"    FragColor = vec4(0.30, 0.30, 0.35, 1.0);\n"
"}\n";

// SHADERS DE LAS CELDAS PINTADAS (cada celda = 2 triángulos dibujados con EBO)


// VERTEX SHADER: recibe la esquina de una celda en UNIDADES DE CELDA (ej: 3, 5)
// y la convierte a NDC (-1..1), que es lo único que entiende OpenGL.
const char* cellVertexShaderSource =
"#version 330 core\n"
"layout (location = 0) in vec2 aCell;\n"    // atributo 0: esquina (x, y) en celdas
"layout (location = 1) in vec3 aColor;\n"   // atributo 1: color (r, g, b) de 0 a 1
"uniform float uCellSize;\n"                // tamaño de la celda en píxeles (40)
"uniform vec2  uWindowSize;\n"              // tamaño actual de la ventana en píxeles
"out vec3 vColor;\n"                        // variable IN-OUT: el color viaja al fragment
"void main()\n"
"{\n"
"    vec2 pixel = aCell * uCellSize;\n"               // paso 1: celda -> píxeles
"    vec2 ndc = (pixel / uWindowSize) * 2.0 - 1.0;\n" // paso 2: píxeles -> NDC [-1, 1]
"    gl_Position = vec4(ndc, 0.0, 1.0);\n"            // z = 0 (2D), w = 1 (homogénea)
"    vColor = aColor;\n"                              // pasa el color sin cambios
"}\n";

// FRAGMENT SHADER: pinta cada píxel del cuadrado con el color recibido.
const char* cellFragmentShaderSource =
"#version 330 core\n"
"in vec3 vColor;\n"            // llega desde el vertex shader (mismo nombre)
"out vec4 FragColor;\n"
"void main()\n"
"{\n"
"    FragColor = vec4(vColor, 1.0);\n"   // alfa 1.0 = totalmente opaco
"}\n";

// FUNCIONES AUXILIARES: COMPILAR SHADERS Y CONVERTIR CELDAS EN TRIÁNGULOS

// Compila UN shader (vertex o fragment). Si tiene errores de sintaxis GLSL,
// los imprime en la consola en vez de fallar en silencio.
unsigned int compilarShader(GLenum tipo, const char* fuente)
{
    unsigned int shader = glCreateShader(tipo);   // pide a la GPU un shader vacío
    glShaderSource(shader, 1, &fuente, NULL);     // le entrega el texto GLSL
    glCompileShader(shader);                      // la GPU lo compila

    int exito;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &exito);  // ¿compiló bien?
    if (!exito)
    {
        char mensaje[512];
        glGetShaderInfoLog(shader, 512, NULL, mensaje); // el error, con número de línea
        std::cout << "ERROR al compilar shader:\n" << mensaje << std::endl;
    }
    return shader;
}

// Une un vertex shader y un fragment shader en un PROGRAMA (momento 3 del PDF).
unsigned int crearProgramaShader(const char* fuenteVertex, const char* fuenteFragment)
{
    unsigned int vertex = compilarShader(GL_VERTEX_SHADER, fuenteVertex);
    unsigned int fragment = compilarShader(GL_FRAGMENT_SHADER, fuenteFragment);

    unsigned int programa = glCreateProgram();
    glAttachShader(programa, vertex);
    glAttachShader(programa, fragment);
    glLinkProgram(programa);   // conecta las salidas del vertex con las entradas del fragment

    int exito;
    glGetProgramiv(programa, GL_LINK_STATUS, &exito);
    if (!exito)
    {
        char mensaje[512];
        glGetProgramInfoLog(programa, 512, NULL, mensaje);
        std::cout << "ERROR al enlazar programa:\n" << mensaje << std::endl;
    }

    // Una vez enlazados, los shaders individuales ya no se necesitan
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    return programa;

}
// Agrega UNA celda (cx, cy) a los arreglos que se subirán a la GPU.
//   vertices: 4 vértices nuevos, cada uno con 5 floats [x, y, r, g, b]
//   indices : 6 índices nuevos = 2 triángulos (0,1,2) y (2,3,0)
//
//        3 ●───────● 2
//          │     ╱ │
//          │   ╱   │
//        0 ●───────● 1
void agregarCelda(std::vector<float>& vertices, std::vector<unsigned int>& indices,
    int cx, int cy, float r, float g, float b)
{
    // ¿Cuántos vértices había ANTES? Como cada vértice ocupa 5 floats, se divide entre 5.
    // Ese número es la "base": los índices de esta celda empiezan ahí (0, 4, 8, 12...).
    unsigned int base = (unsigned int)(vertices.size() / 5);

    // Las 4 esquinas de la celda en unidades de celda
    float x0 = (float)cx;
    float y0 = (float)cy;
    float x1 = x0 + 1.0f;   // la celda mide exactamente 1 unidad de ancho
    float y1 = y0 + 1.0f;   // y 1 unidad de alto

    float esquinas[4][2] = {
        { x0, y0 },   // 0: abajo-izquierda
        { x1, y0 },   // 1: abajo-derecha
        { x1, y1 },   // 2: arriba-derecha
        { x0, y1 }    // 3: arriba-izquierda
    };

    for (int i = 0; i < 4; i++)
    {
        vertices.push_back(esquinas[i][0]);  // x  ─┐ atributo 0 (posición)
        vertices.push_back(esquinas[i][1]);  // y  ─┘
        vertices.push_back(r);               // r  ─┐
        vertices.push_back(g);               // g   ├ atributo 1 (color)
        vertices.push_back(b);               // b  ─┘
    }

    // Triángulo 1: 0-1-2      Triángulo 2: 2-3-0   (desplazados por 'base')
    indices.push_back(base + 0); indices.push_back(base + 1); indices.push_back(base + 2);
    indices.push_back(base + 2); indices.push_back(base + 3); indices.push_back(base + 0);
}

// MANEJADOR DE CLICS DEL MOUSE

void mouse_button_callback(
    GLFWwindow* window,
    int button,
    int action,
    int mods)
{
    // SOLTAR el clic izquierdo: si había un trazo libre en curso, termina.
    // Va ANTES del filtro de ImGui: si el usuario suelta el botón encima
    // del panel, el trazo igual debe terminar.
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE)
    {
        trazandoLibre = false;
        return;
    }

    // Si el clic es sobre la interfaz de ImGui, no se dibuja en la grilla
    if (ImGui::GetIO().WantCaptureMouse) return;

    // De aquí en adelante solo interesa el momento de PRESIONAR
    if (action != GLFW_PRESS) return;

    // CLIC IZQUIERDO
    if (button == GLFW_MOUSE_BUTTON_LEFT)
    {
        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);
        Cell celda = mouseToGrid(xpos, ypos);   // píxel del mouse -> celda

        // Herramienta LIBRE: empieza un trazo nuevo con el primer punto.
        // Los siguientes puntos los agrega cursor_position_callback.
        if (herramientaActual == LIBRE)
        {
            crearFigura(LIBRE, { celda });
            trazandoLibre = true;
            return;
        }

        // Herramientas de clics (línea, círculo, triángulo, cuadrado)
        clicsPendientes.push_back(celda);

        // ¿Ya están todos los clics que necesita la herramienta?
        if ((int)clicsPendientes.size() == clicsNecesarios(herramientaActual))
        {
            crearFigura(herramientaActual, clicsPendientes);
            clicsPendientes.clear();   // listo para la siguiente figura
        }
    }

    // CLIC DERECHO: cancelar la figura que se estaba construyendo
    if (button == GLFW_MOUSE_BUTTON_RIGHT)
    {
        clicsPendientes.clear();
    }
}

// ============================================================================
// MOVIMIENTO DEL MOUSE: GLFW llama esta función cada vez que el cursor se mueve.
// Solo hace algo mientras se está trazando con la herramienta LIBRE.
// ============================================================================
void cursor_position_callback(GLFWwindow* window, double xpos, double ypos)
{
    // Si no se está arrastrando con la herramienta LIBRE, no hay nada que hacer
    if (!trazandoLibre) return;
    if (figuras.empty()) return;   // seguridad: debe existir el trazo en curso

    Cell celda = mouseToGrid(xpos, ypos);   // píxel del mouse -> celda

    // El trazo en curso es la ÚLTIMA figura de la lista (la creó el clic).
    // El '&' hace que 'trazo' sea la figura REAL, no una copia.
    Figura& trazo = figuras.back();

    // Si el mouse sigue en la misma celda del último punto, no se repite
    Cell ultimo = trazo.puntos.back();
    if (celda.x == ultimo.x && celda.y == ultimo.y) return;

    // Nueva celda: se agrega al trazo. Al dibujar, rasterizarTrazo la unirá
    // con la anterior usando Bresenham, así no quedan huecos.
    trazo.puntos.push_back(celda);
}

// ============================================================================
// PANEL DE HERRAMIENTAS (Dear ImGui)
// Se llama UNA VEZ POR FRAME, entre ImGui::NewFrame() e ImGui::Render().
// Los widgets no guardan nada: lo que el usuario elige se guarda en
// herramientaActual y colorActual (modo inmediato).
// ============================================================================
void dibujarPanel()
{
    // Posición y tamaño iniciales SOLO la primera vez (luego el usuario puede moverlo)
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(260, 0), ImGuiCond_FirstUseEver);

    ImGui::Begin("Herramientas");

    // ---------- Selección de herramienta ----------
    ImGui::SeparatorText("Herramienta");
    int h = (int)herramientaActual;   // RadioButton trabaja con int
    ImGui::RadioButton("Linea (Bresenham)", &h, LINEA);
    ImGui::RadioButton("Circulo (punto medio)", &h, CIRCULO);
    ImGui::RadioButton("Triangulo (3 clics)", &h, TRIANGULO);
    ImGui::RadioButton("Cuadrado (2 esquinas)", &h, CUADRADO);
    ImGui::RadioButton("Libre (arrastrar)", &h, LIBRE);
    if (h != (int)herramientaActual)
    {
        herramientaActual = (TipoFigura)h;
        clicsPendientes.clear();   // al cambiar de herramienta se descartan los clics a medias
    }

    // ---------- Color ----------
    ImGui::SeparatorText("Color");
    ImGui::ColorEdit3("Color", colorActual);   // modifica colorActual directamente

    // ---------- Relleno ----------
    ImGui::SeparatorText("Relleno");
    // Checkbox modifica directamente rellenoActivo (true/false)
    ImGui::Checkbox("Rellenar figuras nuevas", &rellenoActivo);
    ImGui::TextWrapped("Aplica a circulo, triangulo y cuadrado (scanline).");

    // ---------- Lienzo ----------
    ImGui::SeparatorText("Lienzo");
    if (ImGui::Button("Deshacer") && !figuras.empty())
        figuras.pop_back();          // quita la última figura
    ImGui::SameLine();
    if (ImGui::Button("Limpiar todo"))
    {
        figuras.clear();
        clicsPendientes.clear();
    }

    // ---------- Información ----------
    ImGui::SeparatorText("Informacion");
    ImGui::Text("Figuras dibujadas: %d", (int)figuras.size());
    if (herramientaActual == LIBRE)
    {
        // La herramienta libre no cuenta clics: se dibuja arrastrando
        ImGui::TextWrapped("Mantener clic izquierdo y arrastrar para dibujar.");
    }
    else
    {
        ImGui::Text("Clics: %d de %d", (int)clicsPendientes.size(), clicsNecesarios(herramientaActual));
        ImGui::TextWrapped("Clic izquierdo: poner puntos. Clic derecho: cancelar.");
    }

    ImGui::End();
}

// ============================================================================
// Nombre legible de cada tipo de figura (para la lista de la ventana)
// ============================================================================
const char* nombreTipo(TipoFigura tipo)
{
    switch (tipo)
    {
    case LINEA:     return "Linea";
    case CIRCULO:   return "Circulo";
    case TRIANGULO: return "Triangulo";
    case CUADRADO:  return "Cuadrado";
    case LIBRE:     return "Libre";
    }
    return "?";
}

// ============================================================================
// VENTANA DE TRANSFORMACIONES (Dear ImGui)
// El usuario elige una figura de la lista y le aplica las 5 transformaciones
// de la diapositiva. Cada botón llama a aplicarTransformacion() con su matriz.
// ============================================================================
void dibujarPanelTransformaciones()
{
    // Posición inicial a la derecha (solo la primera vez)
    ImGui::SetNextWindowPos(ImVec2(490, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 0), ImGuiCond_FirstUseEver);

    ImGui::Begin("Transformaciones");

    // Si se usó Deshacer o Limpiar, la figura seleccionada puede ya no existir:
    // se ajusta para que el índice siempre sea válido (o -1 si no hay figuras).
    if (figuraSeleccionada >= (int)figuras.size())
        figuraSeleccionada = (int)figuras.size() - 1;

    // ---------- Lista de figuras ----------
    ImGui::SeparatorText("Figura seleccionada");
    if (figuras.empty())
    {
        ImGui::TextWrapped("Dibuja una figura primero.");
        ImGui::End();
        return;
    }

    // "##lista": el texto después de ## es un identificador interno (no se muestra)
    if (ImGui::BeginListBox("##lista", ImVec2(-1, 100)))
    {
        for (int i = 0; i < (int)figuras.size(); i++)
        {
            // Texto de cada fila, por ejemplo: "3: Triangulo"
            std::string etiqueta = std::to_string(i + 1) + ": " + nombreTipo(figuras[i].tipo);

            // Selectable: una fila que se resalta si está seleccionada
            if (ImGui::Selectable(etiqueta.c_str(), figuraSeleccionada == i))
                figuraSeleccionada = i;
        }
        ImGui::EndListBox();
    }

    if (figuraSeleccionada < 0)
    {
        ImGui::TextWrapped("Selecciona una figura de la lista.");
        ImGui::End();
        return;
    }

    // ---------- Traslación ----------
    ImGui::SeparatorText("Traslacion");
    static int tx = 2, ty = 0;              // 'static': conservan su valor entre frames
    ImGui::InputInt("tx", &tx);
    ImGui::InputInt("ty", &ty);
    if (ImGui::Button("Trasladar"))
        aplicarTransformacion(figuraSeleccionada, matrizTraslacion((float)tx, (float)ty));

    // ---------- Rotación ----------
    ImGui::SeparatorText("Rotacion");
    static float angulo = 30.0f;
    ImGui::SliderFloat("grados", &angulo, -180.0f, 180.0f, "%.0f");
    if (ImGui::Button("Rotar"))
        aplicarTransformacion(figuraSeleccionada, matrizRotacion(angulo));

    // ---------- Escalamiento ----------
    ImGui::SeparatorText("Escalamiento");
    static float sx = 1.5f, sy = 1.5f;
    ImGui::SliderFloat("sx", &sx, 0.1f, 3.0f, "%.2f");
    ImGui::SliderFloat("sy", &sy, 0.1f, 3.0f, "%.2f");
    if (ImGui::Button("Escalar"))
        aplicarTransformacion(figuraSeleccionada, matrizEscala(sx, sy));

    // ---------- Reflexión ----------
    ImGui::SeparatorText("Reflexion");
    if (ImGui::Button("Espejo eje X"))
        aplicarTransformacion(figuraSeleccionada, matrizReflexionX());
    ImGui::SameLine();
    if (ImGui::Button("Espejo eje Y"))
        aplicarTransformacion(figuraSeleccionada, matrizReflexionY());

    // ---------- Cizallamiento ----------
    ImGui::SeparatorText("Cizallamiento");
    static float shx = 0.5f, shy = 0.0f;
    ImGui::SliderFloat("shx", &shx, -2.0f, 2.0f, "%.2f");
    ImGui::SliderFloat("shy", &shy, -2.0f, 2.0f, "%.2f");
    if (ImGui::Button("Cizallar"))
        aplicarTransformacion(figuraSeleccionada, matrizCizalla(shx, shy));

    // ---------- Reiniciar ----------
    ImGui::Separator();
    // Vuelve a la identidad: la figura recupera su forma y posición ORIGINALES.
    // Es posible porque nunca se modificaron los puntos originales.
    if (ImGui::Button("Reiniciar figura"))
        figuras[figuraSeleccionada].transformacion = matrizIdentidad();

    ImGui::End();
}

// FUNCIÓN PRINCIPAL (MAIN)


int main()
{
    // Inicializa la librería GLFW para la gestión de ventanas
    if (!glfwInit()) return -1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // Crea la ventana gráfica de 800x800 píxeles
    GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Paint con OpenGL", NULL, NULL);
    if (!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);

    // Registra las funciones de callback para eventos de ventana y mouse
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetCursorPosCallback(window, cursor_position_callback);   // movimiento del mouse (trazo libre)

    // Inicializa GLAD para cargar las funciones de OpenGL
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) return -1;

    // ===== INICIALIZACIÓN DE DEAR IMGUI =====
    IMGUI_CHECKVERSION();       // Verifica que los .h y .cpp sean de la misma versión
    ImGui::CreateContext();     // Crea el estado global de ImGui
    ImGui::StyleColorsDark();   // Tema oscuro

    // 'true' = ImGui instala sus callbacks y ENCADENA los nuestros, que ya se
    // registraron arriba con glfwSetMouseButtonCallback. Por eso este bloque
    // va DESPUÉS de registrar nuestros callbacks.
    ImGui_ImplGlfw_InitForOpenGL(window, true);

    // Debe coincidir con nuestro contexto OpenGL 3.3 Core (#version 330 de los shaders)
    ImGui_ImplOpenGL3_Init("#version 330");

    // Programa de la GRILLA (fondo de rayas): compilado con revisión de errores
    unsigned int shaderProgram = crearProgramaShader(vertexShaderSource, fragmentShaderSource);

    // Programa para las celdas pintadas (usa la función con revisión de errores)
    unsigned int cellProgram = crearProgramaShader(cellVertexShaderSource, cellFragmentShaderSource);

    // Geometría del rectángulo que cubre toda la pantalla
    float vertices[] = {
        -1.0f,  1.0f, 0.0f,  -1.0f, -1.0f, 0.0f,   1.0f, -1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f,   1.0f, -1.0f, 0.0f,   1.0f,  1.0f, 0.0f
    };

    unsigned int VAO, VBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // ===== BUFFERS DE LAS CELDAS (momento 4: VAO, VBO, EBO) =====
    unsigned int cellVAO, cellVBO, cellEBO;
    glGenVertexArrays(1, &cellVAO);   // VAO: "la receta" de cómo leer los buffers
    glGenBuffers(1, &cellVBO);        // VBO: guardará los vértices [x, y, r, g, b]
    glGenBuffers(1, &cellEBO);        // EBO: guardará los índices (0,1,2, 2,3,0, ...)

    glBindVertexArray(cellVAO);       // a partir de aquí, el VAO "anota" lo que configuremos
    glBindBuffer(GL_ARRAY_BUFFER, cellVBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cellEBO);  // el VAO recuerda qué EBO usar

    // Atributo 0 = posición: 2 floats, al inicio de cada vértice
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Atributo 1 = color: 3 floats, empieza después de x e y (2 floats = 8 bytes)
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);  // "dejar de anotar" para no modificar este VAO por accidente

    // Ubicaciones de los uniforms del programa de celdas
    int cellSizeLocCeldas = glGetUniformLocation(cellProgram, "uCellSize");
    int windowSizeLocCeldas = glGetUniformLocation(cellProgram, "uWindowSize");

    // Obtención de ubicaciones de variables uniformes para comunicarse con los shaders
    int cellSizeLocation = glGetUniformLocation(shaderProgram, "uCellSize");

    // BUCLE PRINCIPAL DE RENDERIZADO
  
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();   // procesa clics y teclado (aquí se llama mouse_button_callback)

        // [IMGUI] Nuevo frame de interfaz
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        dibujarPanel();   // tu panel de herramientas (en lugar de la demo)
        dibujarPanelTransformaciones();   // ventana de transformaciones (fase 8)

        // ---------- CAPA 1: FONDO ----------
        glClearColor(0.10f, 0.10f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // ---------- CAPA 2: CELDAS PINTADAS ----------
        // Se reconstruye la geometría en cada frame a partir de la lista de figuras
        std::vector<float> verticesCeldas;
        std::vector<unsigned int> indicesCeldas;

        for (const Figura& figura : figuras)                       // cada figura...
        {
            std::vector<Cell> celdas = rasterizarFigura(figura);   // ...se rasteriza con su algoritmo
            for (const Cell& c : celdas)                           // cada celda resultante...
            {
                agregarCelda(verticesCeldas, indicesCeldas,        // ...se vuelve 2 triángulos
                    c.x, c.y,
                    figura.color[0], figura.color[1], figura.color[2]);
            }
        }


        // Clics pendientes de la figura en construcción: se muestran en GRIS
        // para que el usuario vea dónde ya hizo clic.
        for (const Cell& c : clicsPendientes)
        {
            agregarCelda(verticesCeldas, indicesCeldas, c.x, c.y, 0.6f, 0.6f, 0.6f);
        }

        if (!indicesCeldas.empty())   // si no hay nada dibujado, no hay nada que subir
        {
            glBindVertexArray(cellVAO);
            glBindBuffer(GL_ARRAY_BUFFER, cellVBO);
            glBufferData(GL_ARRAY_BUFFER,
                verticesCeldas.size() * sizeof(float),
                verticesCeldas.data(), GL_DYNAMIC_DRAW);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                indicesCeldas.size() * sizeof(unsigned int),
                indicesCeldas.data(), GL_DYNAMIC_DRAW);

            glUseProgram(cellProgram);
            glUniform1f(cellSizeLocCeldas, CELL_SIZE);
            glUniform2f(windowSizeLocCeldas, (float)windowWidth, (float)windowHeight);
            glDrawElements(GL_TRIANGLES, (GLsizei)indicesCeldas.size(), GL_UNSIGNED_INT, 0);
        }

        // ---------- CAPA 3: GRILLA ENCIMA (solo las rayas) ----------
        glUseProgram(shaderProgram);
        glUniform1f(cellSizeLocation, CELL_SIZE);
        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // ---------- CAPA 4: INTERFAZ ----------
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    
    // LIBERACIÓN Y CIERRE

    // [IMGUI] Orden inverso a la inicialización, y antes de glfwTerminate
    // (los backends aún necesitan la ventana y el contexto para liberarse)
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteProgram(shaderProgram);

    glDeleteVertexArrays(1, &cellVAO);
    glDeleteBuffers(1, &cellVBO);
    glDeleteBuffers(1, &cellEBO);
    glDeleteProgram(cellProgram);

    glfwTerminate();

    return 0;
}