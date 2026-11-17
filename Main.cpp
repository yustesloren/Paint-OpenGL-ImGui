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

// Define una línea mediante un punto de inicio y un punto de fin
struct Line
{
    Cell inicio;
    Cell fin;
};

// Define un círculo mediante un centro y un radio entero
struct Circle
{
    Cell centro;
    int radio;
};

// Listas globales para almacenar todas las figuras dibujadas
std::vector<Line> lineas;
std::vector<Circle> circulos;

// VARIABLES DE ESTADO PARA EL MOUSE


bool esperandoFinLinea = false;  // Controla el primer y segundo clic para las líneas
Cell inicioLinea;

bool esperandoRadioCirculo = false; // Controla el centro y el radio para los círculos
Cell centroCirculo;


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


// ALGORITMO DE LÍNEA SIMPLE (INCREMENTAL BÁSICO)


std::vector<Cell> simpleLine(
    int x0,
    int y0,
    int x1,
    int y1)
{
    std::vector<Cell> puntos;

    int dx = x1 - x0;
    int dy = y1 - y0;

    // Calcula cuántos pasos dar tomando la distancia mayor (en X o en Y)
    int steps = std::max(std::abs(dx), std::abs(dy));

    if (steps == 0)
    {
        puntos.push_back({ x0, y0 });
        return puntos;
    }

    // Calcula los incrementos proporcionales para avanzar de forma fluida
    float xInc = dx / (float)steps;
    float yInc = dy / (float)steps;

    float x = x0;
    float y = y0;

    // Ciclo para recorrer cada paso y redondear al entero más cercano
    for (int i = 0; i <= steps; i++)
    {
        puntos.push_back({ (int)(x + 0.5f), (int)(y + 0.5f) });
        x += xInc;
        y += yInc;
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


// SHADERS DE OPENGL (VERTEX Y FRAGMENT SHADER)


const char* vertexShaderSource =
"#version 330 core\n"
"layout (location = 0) in vec3 aPos;\n"
"void main()\n"
"{\n"
"    gl_Position = vec4(aPos, 1.0);\n"
"}\n";

const char* fragmentShaderSource =
"#version 330 core\n"
"out vec4 FragColor;\n"
"uniform float uCellSize;\n"
"uniform int uLineCount;\n"
"uniform ivec2 uLinePoints[200];\n"
"uniform int uCircleCount;\n"
"uniform ivec2 uCirclePoints[400];\n"
"void main()\n"
"{\n"
"    vec2 pixel = gl_FragCoord.xy;\n"
"    ivec2 currentCell = ivec2(floor(pixel / uCellSize));\n"
"    vec2 insideCell = mod(pixel, uCellSize);\n"
"    \n"
"    // Color base de fondo oscuro\n"
"    vec3 color = vec3(0.10, 0.10, 0.15);\n"
"    \n"
"    // Dibuja las líneas de división de la cuadrícula\n"
"    float gridWidth = 1.0;\n"
"    if (insideCell.x < gridWidth || insideCell.y < gridWidth)\n"
"    {\n"
"        color = vec3(0.30, 0.30, 0.35);\n"
"    }\n"
"    \n"
"    // Comprueba si la celda actual forma parte de alguna línea\n"
"    bool isLine = false;\n"
"    for (int i = 0; i < uLineCount; i++)\n"
"    {\n"
"        if (currentCell == uLinePoints[i]) { isLine = true; break; }\n"
"    }\n"
"    \n"
"    // Comprueba si la celda actual forma parte de algún círculo\n"
"    bool isCircle = false;\n"
"    for (int i = 0; i < uCircleCount; i++)\n"
"    {\n"
"        if (currentCell == uCirclePoints[i]) { isCircle = true; break; }\n"
"    }\n"
"    \n"
"    // Asigna colores: Rojo para líneas, Azul para círculos\n"
"    if (isLine)   { color = vec3(1.0, 0.20, 0.10); }\n"
"    if (isCircle) { color = vec3(0.10, 0.75, 1.0); }\n"
"    \n"
"    FragColor = vec4(color, 1.0);\n"
"}\n";


// MANEJADOR DE CLICS DEL MOUSE


void mouse_button_callback(
    GLFWwindow* window,
    int button,
    int action,
    int mods)
{
    // Si el cursor está sobre una ventana de ImGui, el clic es de la interfaz:
    // salimos antes de convertirlo en celda para no pintar debajo del botón.
    if (ImGui::GetIO().WantCaptureMouse) return;

    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);

    Cell celda = mouseToGrid(xpos, ypos); // Convierte posición de pantalla a celda

    // Clic izquierdo: Dibuja Líneas (Primer clic = Inicio, Segundo clic = Fin)
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
    {
        if (!esperandoFinLinea)
        {
            inicioLinea = celda;
            esperandoFinLinea = true;
        }
        else
        {
            Line nuevaLinea = { inicioLinea, celda };
            lineas.push_back(nuevaLinea);
            esperandoFinLinea = false;
        }
    }

    // Clic derecho: Dibuja Círculos (Primer clic = Centro, Segundo clic = Calcula Radio)
    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS)
    {
        if (!esperandoRadioCirculo)
        {
            centroCirculo = celda;
            esperandoRadioCirculo = true;
        }
        else
        {
            // Calcula la distancia euclidiana entre el centro y el clic actual para definir el radio
            int dx = celda.x - centroCirculo.x;
            int dy = celda.y - centroCirculo.y;
            int radio = (int)std::sqrt((float)(dx * dx + dy * dy));

            if (radio < 1) radio = 1;

            Circle nuevoCirculo = { centroCirculo, radio };
            circulos.push_back(nuevoCirculo);
            esperandoRadioCirculo = false;
        }
    }
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

    // Obtención de ubicaciones de variables uniformes para comunicarse con los shaders
    int cellSizeLocation = glGetUniformLocation(shaderProgram, "uCellSize");
    int lineCountLocation = glGetUniformLocation(shaderProgram, "uLineCount");
    int circleCountLocation = glGetUniformLocation(shaderProgram, "uCircleCount");
    int linePointsLocation = glGetUniformLocation(shaderProgram, "uLinePoints[0]");
    int circlePointsLocation = glGetUniformLocation(shaderProgram, "uCirclePoints[0]");

    // BUCLE PRINCIPAL DE RENDERIZADO
  
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents(); // Procesa eventos del sistema operativo (clics, teclado)

        // [IMGUI] Nuevo frame de interfaz. Orden: renderer, plataforma, núcleo.
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // [IMGUI] Declarar la interfaz de este frame (por ahora, la demo oficial)
        


        glClearColor(0.10f, 0.10f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT); // Limpia la pantalla con el color de fondo

        glUseProgram(shaderProgram);
        glUniform1f(cellSizeLocation, CELL_SIZE);

        // Procesa y recopila los puntos de todas las líneas creadas
        std::vector<Cell> puntosLinea;
        for (size_t i = 0; i < lineas.size(); i++)
        {
            std::vector<Cell> puntos = simpleLine(lineas[i].inicio.x, lineas[i].inicio.y, lineas[i].fin.x, lineas[i].fin.y);
            for (size_t j = 0; j < puntos.size(); j++)
            {
                if (puntosLinea.size() < 200) puntosLinea.push_back(puntos[j]);
            }
        }
        glUniform1i(lineCountLocation, (int)puntosLinea.size());
        if (!puntosLinea.empty())
        {
            glUniform2iv(linePointsLocation, (int)puntosLinea.size(), (const GLint*)puntosLinea.data());
        }

        // Procesa y recopila los puntos de todos los círculos creados
        std::vector<Cell> puntosCirculo;
        for (size_t i = 0; i < circulos.size(); i++)
        {
            std::vector<Cell> puntos = midpointCircle(circulos[i].centro.x, circulos[i].centro.y, circulos[i].radio);
            for (size_t j = 0; j < puntos.size(); j++)
            {
                if (puntosCirculo.size() < 400) puntosCirculo.push_back(puntos[j]);
            }
        }
        glUniform1i(circleCountLocation, (int)puntosCirculo.size());
        if (!puntosCirculo.empty())
        {
            glUniform2iv(circlePointsLocation, (int)puntosCirculo.size(), (const GLint*)puntosCirculo.data());
        }

        // Dibuja el cuadro completo en pantalla y actualiza los búferes
        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // [IMGUI] Dibujar la interfaz ENCIMA de la grilla: lo último que se dibuja queda arriba
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
    glfwTerminate();

    return 0;
}


