#ifndef SHADER_H
#define SHADER_H

#include <glad/glad.h>
#include <string>

class Shader {
public:
    Shader(const std::string& vertexPath, const std::string& fragmentPath);
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;

    void use() const;
    GLuint getProgramID() const { return m_programID; }
    bool isValid() const { return m_programID != 0; }

    void setInt(const std::string& name, int value) const;
    void setFloat(const std::string& name, float value) const;

private:
    GLuint m_programID;

    std::string readShaderFile(const std::string& filePath);
    bool compileAndLink(const std::string& vertexSource, const std::string& fragmentSource);
    void checkCompileErrors(GLuint shader, const std::string& type);
    void checkLinkErrors(GLuint program);
    void cleanup();
};

#endif // SHADER_H
