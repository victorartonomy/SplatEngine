#include "ComputeShader.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <glm/gtc/type_ptr.hpp>

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

ComputeShader::~ComputeShader() {
    cleanup();
}

ComputeShader::ComputeShader(ComputeShader&& other) noexcept
    : m_programID(other.m_programID)
{
    other.m_programID = 0;
}

ComputeShader& ComputeShader::operator=(ComputeShader&& other) noexcept {
    if (this != &other) {
        cleanup();
        m_programID = other.m_programID;
        other.m_programID = 0;
    }
    return *this;
}

void ComputeShader::use() const {
    if (m_programID != 0) {
        glUseProgram(m_programID);
    }
}

void ComputeShader::dispatch(GLuint numGroupsX, GLuint numGroupsY, GLuint numGroupsZ) const {
    if (m_programID == 0) {
        std::cerr << "[ERROR] Cannot dispatch invalid compute shader" << std::endl;
        return;
    }

    glDispatchCompute(numGroupsX, numGroupsY, numGroupsZ);

    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

void ComputeShader::setInt(const std::string& name, int value) const {
    GLint location = glGetUniformLocation(m_programID, name.c_str());
    if (location != -1) {
        glProgramUniform1i(m_programID, location, value);
    }
}

void ComputeShader::setUInt(const std::string& name, GLuint value) const {
    GLint location = glGetUniformLocation(m_programID, name.c_str());
    if (location != -1) {
        glProgramUniform1ui(m_programID, location, value);
    }
}

void ComputeShader::setFloat(const std::string& name, float value) const {
    GLint location = glGetUniformLocation(m_programID, name.c_str());
    if (location != -1) {
        glProgramUniform1f(m_programID, location, value);
    }
}

void ComputeShader::setVec3(const std::string& name, const glm::vec3& value) const {
    GLint location = glGetUniformLocation(m_programID, name.c_str());
    if (location != -1) {
        glProgramUniform3fv(m_programID, location, 1, glm::value_ptr(value));
    }
}

void ComputeShader::setIVec2(const std::string& name, const glm::ivec2& value) const {
    GLint location = glGetUniformLocation(m_programID, name.c_str());
    if (location != -1) {
        glProgramUniform2iv(m_programID, location, 1, glm::value_ptr(value));
    }
}

void ComputeShader::setMat4(const std::string& name, const glm::mat4& matrix) const {
    GLint location = glGetUniformLocation(m_programID, name.c_str());
    if (location != -1) {
        glProgramUniformMatrix4fv(m_programID, location, 1, GL_FALSE, glm::value_ptr(matrix));
    }
}

std::string ComputeShader::readShaderFile(const std::string& filePath) {
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
        std::cerr << "[ERROR] Exception: " << e.what() << std::endl;
        return "";
    }
}

bool ComputeShader::compileShader(const std::string& shaderSource) {
    GLuint computeShader = glCreateShader(GL_COMPUTE_SHADER);
    
    const char* sourceCStr = shaderSource.c_str();
    glShaderSource(computeShader, 1, &sourceCStr, nullptr);
    
    glCompileShader(computeShader);
    
    checkCompileErrors(computeShader);
    
    GLint success;
    glGetShaderiv(computeShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glDeleteShader(computeShader);
        return false;
    }

    m_programID = glCreateProgram();
    glAttachShader(m_programID, computeShader);
    glLinkProgram(m_programID);
    
    checkLinkErrors(m_programID);
    
    glGetProgramiv(m_programID, GL_LINK_STATUS, &success);
    if (!success) {
        glDeleteProgram(m_programID);
        m_programID = 0;
        glDeleteShader(computeShader);
        return false;
    }

    glDeleteShader(computeShader);
    
    return true;
}

void ComputeShader::checkCompileErrors(GLuint shader) {
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    
    if (!success) {
        GLint logLength;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
        
        if (logLength > 0) {
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

void ComputeShader::cleanup() {
    if (m_programID != 0) {
        glDeleteProgram(m_programID);
        m_programID = 0;
    }
}
