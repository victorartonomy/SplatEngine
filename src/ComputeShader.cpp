#include "ComputeShader.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <glm/gtc/type_ptr.hpp>

// Constructor — reads the .comp file, compiles it, and links it into a GL program.
// On any failure, m_programID stays 0 and the shader is considered invalid.
// Callers should check isValid() before dispatching.
ComputeShader::ComputeShader(const std::string& shaderPath)
    : m_programID(0)
{
    std::string shaderSource = readShaderFile(shaderPath);
    if (shaderSource.empty()) {
        std::cerr << "[ERROR] Failed to read shader file: " << shaderPath << std::endl;
        return;
    }

    if (!compileShader(shaderSource)) {
        std::cerr << "[ERROR] Failed to compile compute shader from: " << shaderPath << std::endl;
        return;
    }

    std::cout << "[INFO] Compute shader compiled successfully: " << shaderPath << std::endl;
}

// Destructor — releases the GL program object if one was successfully linked.
ComputeShader::~ComputeShader() {
    cleanup();
}

// Move constructor — transfers GL program ownership; nulls out the source so its destructor is a no-op.
ComputeShader::ComputeShader(ComputeShader&& other) noexcept
    : m_programID(other.m_programID)
{
    other.m_programID = 0;
}

// Move assignment — frees any existing program, then steals ownership from `other`.
// Self-assignment guard prevents double-free if someone writes `shader = std::move(shader)`.
ComputeShader& ComputeShader::operator=(ComputeShader&& other) noexcept {
    if (this != &other) {
        cleanup();
        m_programID = other.m_programID;
        other.m_programID = 0;
    }
    return *this;
}

// Bind this program for subsequent GL state changes and dispatches.
// Guard against m_programID == 0 (failed construction) to avoid GL errors.
void ComputeShader::use() const {
    if (m_programID != 0) {
        glUseProgram(m_programID);
    }
}

// Dispatch the compute shader and insert a memory barrier.
// The barrier covers both SSBO writes (GL_SHADER_STORAGE_BARRIER_BIT) and
// image writes (GL_SHADER_IMAGE_ACCESS_BARRIER_BIT) so the next pass sees
// all writes completed. numGroupsX/Y/Z are workgroup counts, not invocation counts.
void ComputeShader::dispatch(GLuint numGroupsX, GLuint numGroupsY, GLuint numGroupsZ) const {
    if (m_programID == 0) {
        std::cerr << "[ERROR] Cannot dispatch invalid compute shader" << std::endl;
        return;
    }

    glDispatchCompute(numGroupsX, numGroupsY, numGroupsZ);

    // Ensure all SSBO and image writes from this dispatch are visible to subsequent passes
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

// Uniform setters — all use glProgramUniform* (DSA variant) so the currently
// bound program is not disturbed. location == -1 means the uniform was optimized
// away by the GLSL compiler; silently skip rather than error.

// Set a single signed integer uniform (e.g., sampler binding points, enum discriminants).
void ComputeShader::setInt(const std::string& name, int value) const {
    GLint location = glGetUniformLocation(m_programID, name.c_str());
    if (location != -1) {
        glProgramUniform1i(m_programID, location, value);
    }
}

// Set a single unsigned integer uniform (e.g., face counts, tile counts).
void ComputeShader::setUInt(const std::string& name, GLuint value) const {
    GLint location = glGetUniformLocation(m_programID, name.c_str());
    if (location != -1) {
        glProgramUniform1ui(m_programID, location, value);
    }
}

// Set a single float uniform (e.g., near/far plane, time).
void ComputeShader::setFloat(const std::string& name, float value) const {
    GLint location = glGetUniformLocation(m_programID, name.c_str());
    if (location != -1) {
        glProgramUniform1f(m_programID, location, value);
    }
}

// Set a vec3 uniform (e.g., camera position, light direction).
// glm::value_ptr returns a pointer to the first float element for the GL API.
void ComputeShader::setVec3(const std::string& name, const glm::vec3& value) const {
    GLint location = glGetUniformLocation(m_programID, name.c_str());
    if (location != -1) {
        glProgramUniform3fv(m_programID, location, 1, glm::value_ptr(value));
    }
}

// Set an ivec2 uniform (e.g., screen resolution, tile grid dimensions).
void ComputeShader::setIVec2(const std::string& name, const glm::ivec2& value) const {
    GLint location = glGetUniformLocation(m_programID, name.c_str());
    if (location != -1) {
        glProgramUniform2iv(m_programID, location, 1, glm::value_ptr(value));
    }
}

// Set a mat4 uniform (e.g., MVP matrix, view-projection).
// GL_FALSE = row-major; GLM stores column-major, which matches GLSL mat4 layout.
void ComputeShader::setMat4(const std::string& name, const glm::mat4& matrix) const {
    GLint location = glGetUniformLocation(m_programID, name.c_str());
    if (location != -1) {
        glProgramUniformMatrix4fv(m_programID, location, 1, GL_FALSE, glm::value_ptr(matrix));
    }
}

// Read the entire .comp source file into a string.
// std::ifstream exceptions are enabled so failures throw rather than silently
// returning partial data. Returns "" on failure; caller logs the error.
std::string ComputeShader::readShaderFile(const std::string& filePath) {
    std::ifstream shaderFile;

    // Throw on failbit (open failed) or badbit (I/O error during read)
    shaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);

    try {
        shaderFile.open(filePath);

        // Stream the entire file into a stringstream in one call
        std::stringstream shaderStream;
        shaderStream << shaderFile.rdbuf();

        shaderFile.close();

        return shaderStream.str();
    }
    catch (std::ifstream::failure& e) {
        std::cerr << "[ERROR] Failed to read shader file: " << filePath << std::endl;
        std::cerr << "[ERROR] Exception: " << e.what() << std::endl;
        return "";
    }
}

// Compile the GLSL source and link it into an OpenGL program object.
// Returns true and sets m_programID on success; returns false and leaves
// m_programID == 0 on compile or link failure.
//
// The shader object is always deleted after linking — only the program
// object needs to persist for subsequent dispatches and uniform queries.
bool ComputeShader::compileShader(const std::string& shaderSource) {
    GLuint computeShader = glCreateShader(GL_COMPUTE_SHADER);

    // Pass source as a single null-terminated string (count = 1, lengths = nullptr)
    const char* sourceCStr = shaderSource.c_str();
    glShaderSource(computeShader, 1, &sourceCStr, nullptr);

    glCompileShader(computeShader);

    // Log any compile errors before checking the status flag
    checkCompileErrors(computeShader);

    GLint success;
    glGetShaderiv(computeShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glDeleteShader(computeShader);
        return false;
    }

    // Create a program, attach the compiled shader, and link
    m_programID = glCreateProgram();
    glAttachShader(m_programID, computeShader);
    glLinkProgram(m_programID);

    // Log any link errors before checking the status flag
    checkLinkErrors(m_programID);

    glGetProgramiv(m_programID, GL_LINK_STATUS, &success);
    if (!success) {
        glDeleteProgram(m_programID);
        m_programID = 0;
        glDeleteShader(computeShader);
        return false;
    }

    // Shader object is no longer needed once the program is linked;
    // the program retains a copy of the compiled binary internally.
    glDeleteShader(computeShader);

    return true;
}

// Query and print the GLSL compiler's info log for a shader object.
// Only fetches the log when compilation failed (GL_COMPILE_STATUS == GL_FALSE),
// avoiding a redundant driver call for successful shaders.
void ComputeShader::checkCompileErrors(GLuint shader) {
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

    if (!success) {
        GLint logLength;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);

        if (logLength > 0) {
            // Allocate exactly `logLength` chars (includes null terminator)
            std::string infoLog(logLength, '\0');
            glGetShaderInfoLog(shader, logLength, nullptr, &infoLog[0]);

            std::cerr << "========================================" << std::endl;
            std::cerr << "[SHADER COMPILATION ERROR]" << std::endl;
            std::cerr << "========================================" << std::endl;
            std::cerr << infoLog << std::endl;
            std::cerr << "========================================" << std::endl;
        }
    }
}

// Query and print the GLSL linker's info log for a program object.
// Same pattern as checkCompileErrors but queries GL_LINK_STATUS on a program.
void ComputeShader::checkLinkErrors(GLuint program) {
    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);

    if (!success) {
        GLint logLength;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);

        if (logLength > 0) {
            std::string infoLog(logLength, '\0');
            glGetProgramInfoLog(program, logLength, nullptr, &infoLog[0]);

            std::cerr << "========================================" << std::endl;
            std::cerr << "[SHADER LINKING ERROR]" << std::endl;
            std::cerr << "========================================" << std::endl;
            std::cerr << infoLog << std::endl;
            std::cerr << "========================================" << std::endl;
        }
    }
}

// Delete the GL program object and reset m_programID to 0.
// Safe to call multiple times (guards on m_programID != 0).
// Called from the destructor and before move-assigning a new program.
void ComputeShader::cleanup() {
    if (m_programID != 0) {
        glDeleteProgram(m_programID);
        m_programID = 0;
    }
}
