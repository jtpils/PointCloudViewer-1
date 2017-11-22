/////////////////////////////////
// A simple point cloud viewer //
// By: Conor Damery            //
/////////////////////////////////

#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <chrono>
#include <thread>

#include <glad/glad.h>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include "imgui_impl.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"
#include "tinyfiledialogs.h"

#define WIN_TITLE "Point Cloud Viewer"
#define WIN_WIDTH 640
#define WIN_HEIGHT 480
#define GL_MAJOR 3
#define GL_MINOR 3

/////////////
// Shaders //
/////////////

// For wireframe shapes (bounds)
static const char *shape_vert =
"#version 330 core\n\
    layout(location = 0) in vec3 POSITION;\n\
    uniform mat4 MVP;\n\
    void main() {\n\
        gl_Position = vec4(POSITION, 1.0) * MVP;\n\
    }\n";

static const char *shape_frag =
"#version 330 core\n\
    out vec4 frag;\n\
    uniform vec4 Color;\n\
    void main() {\n\
        frag = Color;\n\
    }\n";

// For point cloud meshes
static const char *pointcloud_vert =
    "#version 330 core\n\
    layout(location = 0) in vec3 POSITION;\n\
    layout(location = 1) in vec3 NORMAL;\n\
    out vec3 _Normal;\n\
    uniform mat4 MVP;\n\
    void main() {\n\
        gl_Position = vec4(POSITION, 1.0) * MVP;\n\
        _Normal = NORMAL;\n\
    }\n";

static const char *pointcloud_frag =
    "#version 330 core\n\
    in vec3 _Normal;\n\
    out vec4 frag;\n\
    uniform vec3 LightDir;\n\
    uniform vec4 LightCol;\n\
    uniform vec4 AmbientCol;\n\
    void main() {\n\
        float d = dot(_Normal, -LightDir);\n\
        frag = AmbientCol + d * LightCol;\n\
    }\n";

////////////////////////////
// GLFW callback bindings //
////////////////////////////

static void error_callback(int error, const char* description)
{
    fprintf(stderr, "Error: %s\n", description);
}

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(window, GLFW_TRUE);
}

///////////////////////
// Utility functions //
///////////////////////

/**
* Creates a shader program given a vertex and fragment shaders.
*/
static bool createShader(GLuint &prog, const char *vertSrc, const char *fragSrc)
{
    GLint success = 0;
    GLchar errBuff[1024] = { 0 };

	// Create vertex shader
    GLuint vert = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vert, 1, &vertSrc, NULL);
    glCompileShader(vert);

    glGetShaderiv(vert, GL_COMPILE_STATUS, &success);
    if (success == GL_FALSE) {
        glGetShaderInfoLog(vert, sizeof(errBuff), NULL, errBuff);
        std::cerr << errBuff << std::endl;
        return false;
    }

	// Create fragment shader
    GLuint frag = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(frag, 1, &fragSrc, NULL);
    glCompileShader(frag);

    glGetShaderiv(frag, GL_COMPILE_STATUS, &success);
    if (success == GL_FALSE) {
        glGetShaderInfoLog(frag, sizeof(errBuff), NULL, errBuff);
        std::cerr << errBuff << std::endl;
        return false;
    }

	// Create shader program
    prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);

    glLinkProgram(prog);

    glGetProgramiv(prog, GL_LINK_STATUS, &success);
    if (success == GL_FALSE) {
        glGetShaderInfoLog(frag, sizeof(errBuff), NULL, errBuff);
        std::cerr << errBuff << std::endl;
        return false;
    }

    glValidateProgram(prog);

    glGetProgramiv(prog, GL_LINK_STATUS, &success);
    if (success == GL_FALSE) {
        glGetShaderInfoLog(frag, sizeof(errBuff), NULL, errBuff);
        std::cerr << errBuff << std::endl;
        return false;
    }

	// No need for these so we can delete them
    glDeleteShader(vert);
    glDeleteShader(frag);
    return true;
}

/**
* Loads and generates the meshes for rendering.
*/
void loadScene(const std::string filename, GLuint &bounds, std::vector<std::pair<GLuint, size_t>> &meshes) {
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;

	// Load file and check for errors
	std::string err;
	bool ret = tinyobj::LoadObj(shapes, materials, err, filename.c_str());

	if (!err.empty()) std::cerr << err << std::endl;
	if (!ret) exit(1);

	// Calculate min max points and generate vertex arrays
	glm::vec3 min(0), max(0), tmp(0);
	for (size_t i = 0; i < shapes.size(); i++) {
		size_t posCount = shapes[i].mesh.positions.size();
		size_t norCount = shapes[i].mesh.normals.size();
		GLuint mesh = 0;
		GLuint posVBO = 0;
		GLuint norVBO = 0;

		for (size_t j = 0; j < shapes[i].mesh.positions.size();) {
			tmp.x = shapes[i].mesh.positions[j++];
			tmp.y = shapes[i].mesh.positions[j++];
			tmp.z = shapes[i].mesh.positions[j++];
			min.x = tmp.x < min.x ? tmp.x : min.x;
			min.y = tmp.y < min.y ? tmp.y : min.y;
			min.z = tmp.z < min.z ? tmp.z : min.z;
			max.x = tmp.x > max.x ? tmp.x : max.x;
			max.y = tmp.y > max.y ? tmp.y : max.y;
			max.z = tmp.z > max.z ? tmp.z : max.z;
		}

		glGenVertexArrays(1, &mesh);
		glBindVertexArray(mesh);

		// Positions buffer
		glGenBuffers(1, &posVBO);
		glBindBuffer(GL_ARRAY_BUFFER, posVBO);
		glBufferData(GL_ARRAY_BUFFER, posCount * sizeof(float), &shapes[i].mesh.positions[0], GL_STATIC_DRAW);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);
		glEnableVertexAttribArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);

		// Normals buffer
		glGenBuffers(1, &norVBO);
		glBindBuffer(GL_ARRAY_BUFFER, norVBO);
		glBufferData(GL_ARRAY_BUFFER, norCount * sizeof(float), &shapes[i].mesh.normals[0], GL_STATIC_DRAW);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);
		glEnableVertexAttribArray(1);
		glBindBuffer(GL_ARRAY_BUFFER, 0);

		glBindVertexArray(0);

		// This is valid since we have unbinded the VA
		glDeleteBuffers(1, &posVBO);
		glDeleteBuffers(1, &norVBO);

		// Push to list for later drawing
		meshes.emplace_back(std::make_pair(mesh, posCount));
	}

	// Not needed anymore
	shapes.clear();
	materials.clear();

	// Generate bounds mesh
	float boundsData[] = {
		min.x, min.y, min.z,
		max.x, min.y, min.z,
		min.x, max.y, min.z,
		max.x, max.y, min.z,
		min.x, min.y, max.z,
		max.x, min.y, max.z,
		min.x, max.y, max.z,
		max.x, max.y, max.z
	};

	unsigned int boundsIndices[] = {
		0, 1, 3, 1, 2, 0, 2, 3,
		4, 5, 7, 5, 6, 4, 6, 7,
		0, 4, 1, 5, 2, 6, 3, 7
	};

	glGenVertexArrays(1, &bounds);
	glBindVertexArray(bounds);

	GLuint boundsVBO;
	glGenBuffers(1, &boundsVBO);
	glBindBuffer(GL_ARRAY_BUFFER, boundsVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(boundsData), boundsData, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);
	glEnableVertexAttribArray(0);

	GLuint boundsEBO;
	glGenBuffers(1, &boundsEBO);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, boundsEBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(boundsIndices), boundsIndices, GL_STATIC_DRAW);

	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

/**
* Handles the "load scene" event.
*/
void loadSceneFile(GLuint &bounds, std::vector<std::pair<GLuint, size_t>> &meshes) {
    const char *filename = tinyfd_openFileDialog("Open", "", 0, NULL, "scene files", 0);
	if (filename != NULL) {
		// Deletes buffers if any was created
		if (bounds != 0) {
			glDeleteVertexArrays(1, &bounds);
		}
		for (auto m : meshes) {
			glDeleteVertexArrays(1, &m.first);
		}
		bounds = 0;
		meshes.clear();
		
		// Loads the scene meshes
		loadScene(filename, bounds, meshes);
	}
}

/////////////////
// Application //
/////////////////

int main(int argc, char ** args)
{
	// Create window
    GLFWwindow* window;
    glfwSetErrorCallback(error_callback);
    if (!glfwInit())
        exit(EXIT_FAILURE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, GL_MAJOR);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, GL_MINOR);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE); // We are using the core profile for GLSL 330
    window = glfwCreateWindow(WIN_WIDTH, WIN_HEIGHT, WIN_TITLE, NULL, NULL);
    if (!window) {
        glfwTerminate();
        exit(EXIT_FAILURE);
    }
    glfwSetKeyCallback(window, key_callback);
    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc) glfwGetProcAddress);
    glfwSwapInterval(1);

    // Setup ImGui binding
    ImGui_ImplGlfwGL3_Init(window, true);

	// OpenGL config
    printf("%s\n", glGetString(GL_VERSION));
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_DEPTH_CLAMP);
    glEnable(GL_MULTISAMPLE);
    glDisable(GL_CULL_FACE);

	// Rendering vars
	GLuint bounds = 0;
	std::vector<std::pair<GLuint, size_t>> meshes;

    GLuint pointcloundShader;
    createShader(pointcloundShader, pointcloud_vert, pointcloud_frag);

    GLuint shapeShader;
    createShader(shapeShader, shape_vert, shape_frag);

	// Window vars
    float ratio;
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    ratio = width / (float) height;

	// Defines our global axis
    const glm::vec3 right(1, 0, 0);
    const glm::vec3 up(0, 1, 0);
    const glm::vec3 forward(0, 0, 1);

	// Shader vars
    glm::vec3 lightDir(0, -1, 0);
    glm::vec4 lightCol(1, 1, 1, 1);
    glm::vec4 ambientCol(0.01, 0.01, 0.01, 1);

    glm::vec4 boundsColor(0, 1, 0, 0.5f);

	// Camera control vars
    glm::vec3 camPos(0, 0, -1);
    glm::quat camRot; // Used to transform forward dir
    glm::vec3 move(0);
    glm::vec3 angles(0); // Euler angles

    glm::mat4 projT = glm::perspective(70.f, ratio, 0.1f, 1000.f);
    glm::mat4 viewT = glm::lookAt(camPos, camPos + forward * camRot, up); // Looks at the forward direction of rotated camera
    glm::mat4 modelT = glm::scale(glm::vec3(2));
    glm::mat4 mvpT;

    glm::dvec2 mousePos;
    glm::dvec2 mouseDelta;
    glfwGetCursorPos(window, &mousePos.x, &mousePos.y);

	// Game loop vars
    auto start = std::chrono::high_resolution_clock::now();
    auto end = start;

    std::chrono::duration<double, std::nano> frameTime(1.f / 60);
    std::chrono::duration<double, std::nano> elapsed;
    float delta = 0;

	// Config vars
	float scaleExp = 0.9f;
	bool scalePoints = true;
	bool drawBounds = true;

    while (!glfwWindowShouldClose(window))
    {
		// Calculate delta frame time
        end = std::chrono::high_resolution_clock::now();
        elapsed = end - start;
        delta = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() / 1000.f;
        start = end;

        if (elapsed < frameTime)
            std::this_thread::sleep_for(frameTime - elapsed);

        // GUI input
		ImGui_ImplGlfwGL3_NewFrame();

		ImGui::BeginMainMenuBar();
		if (ImGui::BeginMenu("File")) {
			if (ImGui::MenuItem("Load Scene", "", false, true))
				loadSceneFile(bounds, meshes);
			ImGui::EndMenu();
        }
		ImGui::EndMainMenuBar();

		ImGui::Begin("- Rendering -");
		ImGui::Checkbox("Bounds", &drawBounds);
		ImGui::Checkbox("Scaled", &scalePoints);
		if (scalePoints) {
			ImGui::InputFloat("Exponent", &scaleExp, 0.01f, 0.1f, 2);
		}
		ImGui::End();

		// Camera input
        mouseDelta = mousePos;
        glfwGetCursorPos(window, &mousePos.x, &mousePos.y);
        mouseDelta = mousePos - mouseDelta;

        move.x = 0;
        move.z = 0;
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            move.z = 1;
        else if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            move.z = -1;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            move.x = 1;
        else if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            move.x = -1;

        // Update
        if (glfwGetMouseButton(window, 1)) {
            angles.x -= mouseDelta.y * delta;
            angles.y += mouseDelta.x * delta;
            camRot = glm::angleAxis(angles.x, right) * glm::angleAxis(angles.y, up);
        }

        if (glm::length(move) > 0)
            glm::normalize(move);
        camPos += 2.f * move * camRot * delta;

		// Update MVP matrices
        projT = glm::perspective(70.f, ratio, 0.1f, 1000.f);
        viewT = glm::lookAt(camPos, camPos + forward * camRot, up);
        mvpT = projT * viewT * modelT;

        glfwGetFramebufferSize(window, &width, &height);
        ratio = width / (float) height;

        // Draw
        glViewport(0, 0, width, height);

        glClearColor(0.1f, 0.1f, 0.1f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		// Update point cloud shader
        glUseProgram(pointcloundShader);
        glUniformMatrix4fv(glGetUniformLocation(pointcloundShader, "MVP"), 1, GL_TRUE, glm::value_ptr(mvpT));
        glUniform3fv(glGetUniformLocation(pointcloundShader, "LightDir"), 1, glm::value_ptr(lightDir));
        glUniform4fv(glGetUniformLocation(pointcloundShader, "LightCol"), 1, glm::value_ptr(lightCol));
        glUniform4fv(glGetUniformLocation(pointcloundShader, "AmbientCol"), 1, glm::value_ptr(ambientCol));

		if (scalePoints) {
			const float size = (1.f / pow(glm::length(camPos), scaleExp)) * 20;
			glPointSize(size);
		} else {
			glPointSize(1.f);
		}

        for (auto mesh : meshes) {
            glBindVertexArray(mesh.first);
            glDrawArrays(GL_POINTS, 0, mesh.second);
        }

		// Update shape shader
        glUseProgram(shapeShader);
        glUniformMatrix4fv(glGetUniformLocation(shapeShader, "MVP"), 1, GL_TRUE, glm::value_ptr(mvpT));
        glUniform4fv(glGetUniformLocation(shapeShader, "Color"), 1, glm::value_ptr(boundsColor));

		if (bounds && drawBounds) {
			glBindVertexArray(bounds);
			glDrawElements(GL_LINES, 24, GL_UNSIGNED_INT, 0);
		}

        ImGui::Render();

		// Display
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

	// Clean resources
    for (auto mesh : meshes)
        glDeleteVertexArrays(1, &mesh.first);
    glDeleteVertexArrays(1, &bounds);

    glfwDestroyWindow(window);
    glfwTerminate();
    exit(EXIT_SUCCESS);
}
