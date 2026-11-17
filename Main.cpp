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
        glfwPollEvents(); // Procesa eventos del sistema operativo (clics, teclado)

        // [IMGUI] Nuevo frame de interfaz. Orden: renderer, plataforma, núcleo.
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // [IMGUI] Declarar la interfaz de este frame (por ahora, la demo oficial)
        
        ImGui::ShowDemoWindow();

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

        //  CELDAS PINTADITAS
// 1) Armar la geometría de ESTE frame (por ahora: una celda fija de prueba)
        std::vector<float> verticesCeldas;
        std::vector<unsigned int> indicesCeldas;
        agregarCelda(verticesCeldas, indicesCeldas, 3, 5, 1.0f, 0.0f, 0.0f);  // celda (3,5) roja

        // 2) Subir los datos a la GPU
        glBindVertexArray(cellVAO);                 // al enlazar el VAO, también queda activo su EBO
        glBindBuffer(GL_ARRAY_BUFFER, cellVBO);
        glBufferData(GL_ARRAY_BUFFER,
            verticesCeldas.size() * sizeof(float),       // tamaño en BYTES
            verticesCeldas.data(),                       // puntero a los datos
            GL_DYNAMIC_DRAW);                            // "cambiarán seguido"
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
            indicesCeldas.size() * sizeof(unsigned int),
            indicesCeldas.data(),
            GL_DYNAMIC_DRAW);

        // 3) Dibujar con el programa de celdas
        glUseProgram(cellProgram);
        glUniform1f(cellSizeLocCeldas, CELL_SIZE);
        glUniform2f(windowSizeLocCeldas, (float)windowWidth, (float)windowHeight);
        glDrawElements(GL_TRIANGLES,                    // dibujar triángulos
            (GLsizei)indicesCeldas.size(),   // cuántos ÍNDICES (6 por celda)
            GL_UNSIGNED_INT,                 // tipo de los índices
            0);                              // empezar desde el inicio del EBO

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

    glDeleteVertexArrays(1, &cellVAO);
    glDeleteBuffers(1, &cellVBO);
    glDeleteBuffers(1, &cellEBO);
    glDeleteProgram(cellProgram);

    glfwTerminate();

    return 0;
}


