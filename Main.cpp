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

// Tipos de figura que sabe dibujar el programa.
// (En las siguientes fases agregaremos TRIANGULO, CUADRADO y LIBRE.)
//enum: una lista de nombres para opciones fijas.
enum TipoFigura { LINEA, CIRCULO };

// Una figura se guarda por sus PUNTOS DE CONTROL (los clics que la definen),
// NO por sus celdas pintadas. Las celdas se calculan cada frame con el algoritmo.
//   LINEA   -> puntos[0] = inicio,  puntos[1] = fin
//   CIRCULO -> puntos[0] = centro,  puntos[1] = un punto del borde (define el radio)
struct Figura
{
    TipoFigura tipo = LINEA;                  // valor inicial por defecto (evita "basura")
    std::vector<Cell> puntos;                 // un vector siempre empieza vacío, no necesita valor inicial
    float color[3] = { 1.0f, 1.0f, 1.0f };    // r, g, b entre 0.0 y 1.0 (blanco por defecto)         // r, g, b entre 0.0 y 1.0
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
// RASTERIZAR UNA FIGURA: convierte sus puntos de control en la lista de celdas
// que hay que pintar, usando el algoritmo que corresponde a su tipo.
// ============================================================================
std::vector<Cell> rasterizarFigura(const Figura& figura)
{
    if (figura.tipo == LINEA)
    {
        Cell inicio = figura.puntos[0];
        Cell fin    = figura.puntos[1];
        // Algoritmo de Bresenham (diapositiva 13): solo aritmética entera
        return bresenhamLine(inicio.x, inicio.y, fin.x, fin.y);
    }

    if (figura.tipo == CIRCULO)
    {
        Cell centro = figura.puntos[0];
        Cell borde  = figura.puntos[1];

        // Radio = distancia euclidiana entre el centro y el punto del borde
        int dx = borde.x - centro.x;
        int dy = borde.y - centro.y;
        int radio = (int)std::sqrt((float)(dx * dx + dy * dy));
        if (radio < 1) radio = 1;

        return midpointCircle(centro.x, centro.y, radio);
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
    // Se COPIA el color actual: si luego el usuario cambia de color,
    // esta figura conserva el suyo.
    for (int i = 0; i < 3; i++)
        nueva.color[i] = colorActual[i];

    figuras.push_back(nueva);
}


// ============================================================================
// Cuántos clics necesita cada herramienta para crear su figura.
// (Fase 5: aquí se agregan TRIANGULO -> 3 y CUADRADO -> 2)
// ============================================================================
int clicsNecesarios(TipoFigura tipo)
{
    switch (tipo)
    {
    case LINEA:   return 2;   // inicio y fin
    case CIRCULO: return 2;   // centro y un punto del borde
    }
    return 2;
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

// MANEJADOR DE CLICS DEL MOUSE

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


void mouse_button_callback(
    GLFWwindow* window,
    int button,
    int action,
    int mods)
{
    // Si el clic es sobre la interfaz de ImGui, no se dibuja en la grilla
    if (ImGui::GetIO().WantCaptureMouse) return;

    // Solo interesa el momento en que se PRESIONA un botón (no cuando se suelta)
    if (action != GLFW_PRESS) return;

    // CLIC IZQUIERDO: agregar un punto a la figura en construcción
    if (button == GLFW_MOUSE_BUTTON_LEFT)
    {
        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);
        Cell celda = mouseToGrid(xpos, ypos);   // píxel del mouse -> celda

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
    if (h != (int)herramientaActual)
    {
        herramientaActual = (TipoFigura)h;
        clicsPendientes.clear();   // al cambiar de herramienta se descartan los clics a medias
    }

    // ---------- Color ----------
    ImGui::SeparatorText("Color");
    ImGui::ColorEdit3("Color", colorActual);   // modifica colorActual directamente

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
    ImGui::Text("Clics: %d de %d", (int)clicsPendientes.size(), clicsNecesarios(herramientaActual));
    ImGui::TextWrapped("Clic izquierdo: poner puntos. Clic derecho: cancelar.");

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
    GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Linea y Circulo", NULL, NULL);
    if (!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);

    // Registra las funciones de callback para eventos de ventana y mouse
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);

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

    // Compilación y enlace de los shaders en la GPU
    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);

    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);

    unsigned int shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

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
    int lineCountLocation = glGetUniformLocation(shaderProgram, "uLineCount");
    int circleCountLocation = glGetUniformLocation(shaderProgram, "uCircleCount");
    int linePointsLocation = glGetUniformLocation(shaderProgram, "uLinePoints[0]");
    int circlePointsLocation = glGetUniformLocation(shaderProgram, "uCirclePoints[0]");

    // BUCLE PRINCIPAL DE RENDERIZADO
  
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();   // procesa clics y teclado (aquí se llama mouse_button_callback)

        // [IMGUI] Nuevo frame de interfaz
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        dibujarPanel();   // tu panel de herramientas (en lugar de la demo)

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


