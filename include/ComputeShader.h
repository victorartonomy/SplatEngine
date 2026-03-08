#ifndef COMPUTE_SHADER_H
#define COMPUTE_SHADER_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>

class ComputeShader {
public:
    ComputeShader(const std::string& shaderPath);
    ~ComputeShader();

    ComputeShader(const ComputeShader&) = delete;
    ComputeShader& operator=(const ComputeShader&) = delete;

    ComputeShader(ComputeShader&& other) noexcept;
    ComputeShader& operator=(ComputeShader&& other) noexcept;

    void use() const;
    void dispatch(GLuint numGroupsX, GLuint numGroupsY, GLuint numGroupsZ) const;

    void setInt(const std::string& name, int value) const;
    void setUInt(const std::string& name, GLuint value) const;
    void setFloat(const std::string& name, float value) const;
    void setVec3(const std::string& name, const glm::vec3& value) const;
    void setIVec2(const std::string& name, const glm::ivec2& value) const;
    void setMat4(const std::string& name, const glm::mat4& matrix) const;

    GLuint getProgramID() const { return m_programID; }
    bool isValid() const { return m_programID != 0; }

private:
    GLuint m_programID;

    std::string readShaderFile(const std::string& filePath);
    bool compileShader(const std::string& shaderSource);
    void checkCompileErrors(GLuint shader);
    void checkLinkErrors(GLuint program);
    void cleanup();
};

#endif // COMPUTE_SHADER_H
