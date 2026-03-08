#include "Shader.h"
#include <fstream>
#include <sstream>
#include <iostream>

Shader::Shader(const std::string& vertexPath, const std::string& fragmentPath)
    : m_programID(0)
{
    std::string vertexSource = readShaderFile(vertexPath);
    std::string fragmentSource = readShaderFile(fragmentPath);
    
    if (vertexSource.empty() || fragmentSource.empty()) {
        std::cerr << "[ERROR] Failed to read shader files" << std::endl;
        return;
    }

    if (!compileAndLink(vertexSource, fragmentSource)) {
        std::cerr << "[ERROR] Failed to compile/link shaders" << std::endl;
        return;
    }

    std::cout << "[INFO] Graphics shader compiled: " << vertexPath << " + " << fragmentPath << std::endl;
}

Shader::~Shader() {
    cleanup();
}

Shader::Shader(Shader&& other) noexcept
    : m_programID(other.m_programID)
{
    other.m_programID = 0;
}

Shader& Shader::operator=(Shader&& other) noexcept {
    if (this != &other) {
        cleanup();
        m_programID = other.m_programID;
        other.m_programID = 0;
    }
    return *this;
}

void Shader::use() const {
    if (m_programID != 0) {
        glUseProgram(m_programID);
    }
}

void Shader::setInt(const std::string& name, int value) const {
    GLint location = glGetUniformLocation(m_programID, name.c_str());
    if (location != -1) {
        glUniform1i(location, value);
    }
}

void Shader::setFloat(const std::string& name, float value) const {
    GLint location = glGetUniformLocation(m_programID, name.c_str());
    if (location != -1) {
        glUniform1f(location, value);
    }
}

std::string Shader::readShaderFile(const std::string& filePath) {
    std::ifstream shaderFile;
    shaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    
    try {
        shaderFile.open(filePath);
        std::stringstream shaderStream;
        shaderStream << shaderFile.rdbuf();
        shaderFile.close();
        return shaderStream.str();
    }
    catch (std::ifstream::failure& e) {
        std::cerr << "[ERROR] Failed to read shader file: " << filePath << std::endl;
        return "";
    }
}

bool Shader::compileAndLink(const std::string& vertexSource, const std::string& fragmentSource) {
    const char* vShaderCode = vertexSource.c_str();
    const char* fShaderCode = fragmentSource.c_str();

    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vShaderCode, nullptr);
    glCompileShader(vertexShader);
    checkCompileErrors(vertexShader, "VERTEX");
    
    GLint vSuccess;
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &vSuccess);
    if (!vSuccess) {
        glDeleteShader(vertexShader);
        return false;
    }

    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fShaderCode, nullptr);
    glCompileShader(fragmentShader);
    checkCompileErrors(fragmentShader, "FRAGMENT");
    
    GLint fSuccess;
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &fSuccess);
    if (!fSuccess) {
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return false;
    }

    m_programID = glCreateProgram();
    glAttachShader(m_programID, vertexShader);
    glAttachShader(m_programID, fragmentShader);
    glLinkProgram(m_programID);
    checkLinkErrors(m_programID);
    
    GLint linkSuccess;
    glGetProgramiv(m_programID, GL_LINK_STATUS, &linkSuccess);
    
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    
    if (!linkSuccess) {
        glDeleteProgram(m_programID);
        m_programID = 0;
        return false;
    }

    return true;
}

void Shader::checkCompileErrors(GLuint shader, const std::string& type) {
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    
    if (!success) {
        GLint logLength;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
        
        if (logLength > 0) {
            std::string infoLog(logLength, '\0');
            glGetShaderInfoLog(shader, logLength, nullptr, &infoLog[0]);
            
            std::cerr << "========================================" << std::endl;
            std::cerr << "[" << type << " SHADER COMPILATION ERROR]" << std::endl;
            std::cerr << "========================================" << std::endl;
            std::cerr << infoLog << std::endl;
            std::cerr << "========================================" << std::endl;
        }
    }
}

void Shader::checkLinkErrors(GLuint program) {
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

void Shader::cleanup() {
    if (m_programID != 0) {
        glDeleteProgram(m_programID);
        m_programID = 0;
    }
}
