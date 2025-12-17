#define _USE_MATH_DEFINES   // Enables math constants like M_PI
#include <cmath>  // Math functions and constants
#include <vector>   // std::vector container
#include <algorithm> // Algorithms like std::sort, std::find
#include <string>                     // std::string class
#include <cstdint>                    // Fixed-width integer types (e.g., uint8_t)

// OpenGL and related libraries
#include <glew.h>                     // GLEW for managing OpenGL extensions
#include <glfw3.h>                    // GLFW for windowing and input
#include <AL/al.h>                    // OpenAL for audio (main functions)
#include <AL/alc.h>                   // OpenAL context management
#include <glm/glm.hpp>               // GLM for vector/matrix math

#define STB_IMAGE_IMPLEMENTATION 
#include "stb_image.h"               // STB image loader

// Standard utilities
#include <iostream>                  // Standard input/output streams
#include <map>                       // std::map container

#include <GL/freeglut.h>             // FreeGLUT for rendering text, window handling

const float POP_DURATION = 0.5f;
const float POP_SCALE = 1.3f;

ALuint correctSound = 0;
ALuint incorrectSound = 0;

ALuint backgroundMusic = 0;
ALuint musicSource = 0;

bool pendingUnlock = false;
float unlockTimer = 0.0f;
std::string animalToUnlock = "";


struct Message {
    std::string text = "";
    float x = 0.0f;
    float y = 0.0f;
    float timer = 0.0f;
    glm::vec3 color = { 1.0f, 1.0f, 0.0f };
    float scale = 1.0f;
};

Message feedbackMessage;

GLuint soundboardTex = 0;
GLuint playTex = 0;
GLuint pauseTex = 0;
GLuint lockTex = 0;
GLuint soundboardBgTex = 0;
GLuint backgroundTex = 0;

GLuint LoadTexture(const char* filepath) {
    int width, height, channels;
    unsigned char* data = stbi_load(filepath, &width, &height, &channels, STBI_rgb_alpha);
    if (!data) {
        std::cerr << "Failed to load texture: " << filepath << std::endl;
        return 0;
    }

    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    stbi_image_free(data);
    return texture;
}

GLFWimage LoadIconImage(const char* filepath) {
    GLFWimage icon = { 0, 0, nullptr };
    icon.pixels = stbi_load(filepath, &icon.width, &icon.height, nullptr, 4); // RGBA channels
    if (!icon.pixels) {
        std::cerr << "Failed to load icon: " << filepath << std::endl;
    }
    return icon;
}

ALuint LoadSound(const char* filepath) {
    FILE* file = nullptr;
    if (fopen_s(&file, filepath, "rb") != 0 || !file) {
        std::cerr << "Failed to open WAV file: " << filepath << std::endl;
        return 0;
    }

    // Read the header
    char header[44];
    if (fread(header, 1, 44, file) != 44) {
        std::cerr << "Invalid WAV header (too small)" << std::endl;
        fclose(file);
        return 0;
    }

    // Check RIFF header
    if (memcmp(header, "RIFF", 4) != 0) {
        std::cerr << "Not a RIFF file" << std::endl;
        fclose(file);
        return 0;
    }

    // Check WAVE format
    if (memcmp(header + 8, "WAVEfmt ", 8) != 0) {
        std::cerr << "Not a WAVE file" << std::endl;
        fclose(file);
        return 0;
    }

    // Extract audio format information
    unsigned short audioFormat = *(unsigned short*)(header + 20);
    if (audioFormat != 1) {  // 1 = PCM
        std::cerr << "Only PCM format supported" << std::endl;
        fclose(file);
        return 0;
    }

    unsigned short channels = *(unsigned short*)(header + 22);
    unsigned sampleRate = *(unsigned*)(header + 24);
    unsigned short bitsPerSample = *(unsigned short*)(header + 34);
    unsigned dataSize = *(unsigned*)(header + 40);

    // Validate format
    if (channels < 1 || channels > 2) {
        std::cerr << "Unsupported number of channels: " << channels << std::endl;
        fclose(file);
        return 0;
    }

    if (bitsPerSample != 8 && bitsPerSample != 16) {
        std::cerr << "Unsupported bits per sample: " << bitsPerSample << std::endl;
        fclose(file);
        return 0;
    }

    // Read audio data
    std::vector<char> audioData(dataSize);
    if (fread(audioData.data(), 1, dataSize, file) != dataSize) {
        std::cerr << "Failed to read audio data" << std::endl;
        fclose(file);
        return 0;
    }
    fclose(file);

    // Determine OpenAL format
    ALenum format;
    if (channels == 1 && bitsPerSample == 8) format = AL_FORMAT_MONO8;
    else if (channels == 1 && bitsPerSample == 16) format = AL_FORMAT_MONO16;
    else if (channels == 2 && bitsPerSample == 8) format = AL_FORMAT_STEREO8;
    else if (channels == 2 && bitsPerSample == 16) format = AL_FORMAT_STEREO16;
    else {
        std::cerr << "Unsupported WAV format" << std::endl;
        return 0;
    }

    // Create OpenAL buffer
    ALuint buffer;
    alGenBuffers(1, &buffer);
    alBufferData(buffer, format, audioData.data(), dataSize, sampleRate);

    // Check for errors
    ALenum error = alGetError();
    if (error != AL_NO_ERROR) {
        std::cerr << "OpenAL error (" << error << ") loading: " << filepath << std::endl;
        if (buffer) alDeleteBuffers(1, &buffer);
        return 0;
    }

    return buffer;
}

struct SoundButton {
    float x, y, width, height;
    std::string label;
    bool isPlaying = false;
    bool unlocked = false;
    glm::vec3 color;
    ALuint soundSource = 0;
    ALuint soundBuffer = 0;
    float playBtnX, playBtnY;
    float playBtnSize = 0.08f;
    float lockX, lockY;
};

struct Animal {
    GLuint texture = 0;
    ALuint soundBuffer = 0;
    float x = 0.0f;
    float y = 0.0f;
    float soundX = 0.0f;
    float soundY = 0.0f;
    std::string displayName = "";
    bool unlocked = false;
    bool soundUnlocked = false;
    bool found = false;
    float scale = 1.0f;
    bool isPopping = false;
    float popTimer = 0.0f;
};

std::map<std::string, Animal> animals;
std::vector<std::string> animalOrder = { "cat", "bird", "lion", "elephant", "dog", "cow" };
std::vector<SoundButton> soundButtons;
std::vector<ALuint> tempSources; // For managing temporary sound sources

void DrawAnimal(const Animal& a) {
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, a.texture);

    glPushMatrix();
    glTranslatef(a.x + 0.1f, a.y + 0.1f, 0);
    glScalef(a.scale, a.scale, 1.0f);
    glTranslatef(-(a.x + 0.1f), -(a.y + 0.1f), 0);

    glBegin(GL_QUADS);
    glTexCoord2f(0, 1); glVertex2f(a.x, a.y);
    glTexCoord2f(1, 1); glVertex2f(a.x + 0.2f, a.y);
    glTexCoord2f(1, 0); glVertex2f(a.x + 0.2f, a.y + 0.2f);
    glTexCoord2f(0, 0); glVertex2f(a.x, a.y + 0.2f);
    glEnd();

    glPopMatrix();
}

bool IsClicked(float mouseX, float mouseY, float x, float y) {
    return mouseX >= x && mouseX <= x + 0.2f && mouseY >= y && mouseY <= y + 0.2f;
}

void DrawBackground(GLuint texture) {
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);

    glBegin(GL_QUADS);
    glTexCoord2f(0, 1); glVertex2f(-1, -1);
    glTexCoord2f(1, 1); glVertex2f(1, -1);
    glTexCoord2f(1, 0); glVertex2f(1, 1);
    glTexCoord2f(0, 0); glVertex2f(-1, 1);
    glEnd();

    glDisable(GL_TEXTURE_2D);
}

void UpdateAnimations(float deltaTime) {
    for (auto& pair : animals) {
        Animal& animal = pair.second;
        if (animal.isPopping) {
            animal.popTimer += deltaTime;
            float progress = animal.popTimer / POP_DURATION;

            if (progress < 0.5f) {
                animal.scale = 1.0f + (POP_SCALE - 1.0f) * (progress * 2);
            }
            else {
                animal.scale = POP_SCALE - (POP_SCALE - 1.0f) * ((progress - 0.5f) * 2);
            }

            if (animal.popTimer >= POP_DURATION) {
                animal.isPopping = false;
                animal.scale = 1.0f;
            }
        }
    }
}

void UpdateMessages(float deltaTime) {
    if (feedbackMessage.timer > 0.0f) {
        feedbackMessage.timer -= deltaTime;
        if (feedbackMessage.timer <= 0.0f) {
            feedbackMessage.text = "";
        }
    }
}

void CleanUpTempSources() {
    // Clean up finished temporary sources
    tempSources.erase(std::remove_if(tempSources.begin(), tempSources.end(),
        [](ALuint source) {
            ALint state;
            alGetSourcei(source, AL_SOURCE_STATE, &state);
            if (state != AL_PLAYING) {
                alDeleteSources(1, &source);
                return true;
            }
            return false;
        }), tempSources.end());

    // Update sound button states (if any sound finishes)
    for (auto& button : soundButtons) {
        if (button.isPlaying) {
            ALint state;
            alGetSourcei(button.soundSource, AL_SOURCE_STATE, &state);
            if (state != AL_PLAYING) {
                button.isPlaying = false; // Reset to play.png
            }
        }
    }
}

void DrawText(GLFWwindow* window, const std::string& text, float normX, float normY, glm::vec3 color = { 0.0f, 0.0f, 0.0f }) {
    glPushAttrib(GL_ALL_ATTRIB_BITS);

    glDisable(GL_TEXTURE_2D);   // Disable textures so background doesn't affect text
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);        // Avoid blending hiding solid text

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();

    int width, height;
    glfwGetWindowSize(window, &width, &height);
    glOrtho(0, width, height, 0, -1, 1);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glColor3f(color.r, color.g, color.b);

    int x = (int)((normX + 1.0f) * width / 2.0f);
    int y = (int)((1.0f - normY) * height / 2.0f);
    glRasterPos2i(x, y);

    for (char c : text) {
        glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18, c);
    }

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopAttrib();
}

void DrawSoundboard(GLuint texture) {
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);

    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(-1.0f, -1.0f);
    glTexCoord2f(1, 0); glVertex2f(-0.6f, -1.0f);
    glTexCoord2f(1, 1); glVertex2f(-0.6f, 1.0f);
    glTexCoord2f(0, 1); glVertex2f(-1.0f, 1.0f);
    glEnd();

    glDisable(GL_TEXTURE_2D);
}

void DrawRoundedRect(float x, float y, float width, float height, float radius) {
    const int segments = 10;

    glBegin(GL_QUADS);
    glVertex2f(x + radius, y);
    glVertex2f(x + width - radius, y);
    glVertex2f(x + width - radius, y + height);
    glVertex2f(x + radius, y + height);
    glEnd();

    glBegin(GL_QUADS);
    glVertex2f(x, y + radius);
    glVertex2f(x + radius, y + radius);
    glVertex2f(x + radius, y + height - radius);
    glVertex2f(x, y + height - radius);

    glVertex2f(x + width - radius, y + radius);
    glVertex2f(x + width, y + radius);
    glVertex2f(x + width, y + height - radius);
    glVertex2f(x + width - radius, y + height - radius);
    glEnd();

    glBegin(GL_QUADS);
    glVertex2f(x + radius, y + height - radius);
    glVertex2f(x + width - radius, y + height - radius);
    glVertex2f(x + width - radius, y + height);
    glVertex2f(x + radius, y + height);

    glVertex2f(x + radius, y);
    glVertex2f(x + width - radius, y);
    glVertex2f(x + width - radius, y + radius);
    glVertex2f(x + radius, y + radius);
    glEnd();

    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(x + radius, y + radius);
    for (int i = 0; i <= segments; ++i) {
        float angle = M_PI + i * (M_PI / 2) / segments;
        glVertex2f(x + radius + cos(angle) * radius, y + radius + sin(angle) * radius);
    }
    glEnd();

    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(x + width - radius, y + radius);
    for (int i = 0; i <= segments; ++i) {
        float angle = 1.5f * M_PI + i * (M_PI / 2) / segments;
        glVertex2f(x + width - radius + cos(angle) * radius, y + radius + sin(angle) * radius);
    }
    glEnd();

    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(x + width - radius, y + height - radius);
    for (int i = 0; i <= segments; ++i) {
        float angle = 0 + i * (M_PI / 2) / segments;
        glVertex2f(x + width - radius + cos(angle) * radius, y + height - radius + sin(angle) * radius);
    }
    glEnd();

    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(x + radius, y + height - radius);
    for (int i = 0; i <= segments; ++i) {
        float angle = 0.5f * M_PI + i * (M_PI / 2) / segments;
        glVertex2f(x + radius + cos(angle) * radius, y + height - radius + sin(angle) * radius);
    }
    glEnd();
}

void DrawSoundboardUI(GLFWwindow* window) {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glColor3f(1.0f, 1.0f, 1.0f);

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, soundboardTex);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 1); glVertex2f(-1.0f, -1.0f);
    glTexCoord2f(1, 1); glVertex2f(-0.5f, -1.0f);
    glTexCoord2f(1, 0); glVertex2f(-0.5f, 1.0f);
    glTexCoord2f(0, 0); glVertex2f(-1.0f, 1.0f);
    glEnd();
    glDisable(GL_TEXTURE_2D);

    glColor3f(1.0f, 1.0f, 0.0f);
    DrawText(window, "FIND THE", -0.82f, 0.85f);
    DrawText(window, "HIDDEN ANIMALS", -0.87f, 0.78f);

    for (const auto& button : soundButtons) {
        glColor3f(button.color.r, button.color.g, button.color.b);
        DrawRoundedRect(button.x, button.y, button.width, button.height, 0.05f);

        if (button.unlocked) {
            float textX = button.x + 0.03f;
            float textY = button.y + button.height / 2.0f - 0.02f;

            glDisable(GL_TEXTURE_2D);
            glColor3f(0.0f, 0.0f, 0.0f);
            DrawText(window, button.label, textX, textY);
            glEnable(GL_TEXTURE_2D);
        }

        if (button.unlocked) {
            glEnable(GL_TEXTURE_2D);
            glColor3f(1.0f, 1.0f, 1.0f);
            glBindTexture(GL_TEXTURE_2D, button.isPlaying ? pauseTex : playTex);
            glBegin(GL_QUADS);
            glTexCoord2f(0, 1); glVertex2f(button.playBtnX, button.playBtnY);
            glTexCoord2f(1, 1); glVertex2f(button.playBtnX + button.playBtnSize, button.playBtnY);
            glTexCoord2f(1, 0); glVertex2f(button.playBtnX + button.playBtnSize, button.playBtnY + button.playBtnSize);
            glTexCoord2f(0, 0); glVertex2f(button.playBtnX, button.playBtnY + button.playBtnSize);
            glEnd();
            glDisable(GL_TEXTURE_2D);
        }
        else {
            glEnable(GL_TEXTURE_2D);
            glColor3f(1.0f, 1.0f, 1.0f);
            glBindTexture(GL_TEXTURE_2D, lockTex);
            glBegin(GL_QUADS);
            glTexCoord2f(0, 1); glVertex2f(button.lockX, button.lockY);
            glTexCoord2f(1, 1); glVertex2f(button.lockX + button.playBtnSize, button.lockY);
            glTexCoord2f(1, 0); glVertex2f(button.lockX + button.playBtnSize, button.lockY + button.playBtnSize);
            glTexCoord2f(0, 0); glVertex2f(button.lockX, button.lockY + button.playBtnSize);
            glEnd();
            glDisable(GL_TEXTURE_2D);
        }
    }
}

void InitializeAnimals() {
    soundboardTex = LoadTexture("assets/soundboard.jpg");
    backgroundTex = LoadTexture("assets/backg.jpg");
    lockTex = LoadTexture("assets/lock.png");
    playTex = LoadTexture("assets/play.png");
    pauseTex = LoadTexture("assets/pause.png");

    correctSound = LoadSound("assets/correct.wav");
    incorrectSound = LoadSound("assets/incorrect.wav");

    const glm::vec3 goldenColor(0.906f, 0.737f, 0.369f);

    // Initialize animals with their sound buffers
    animals["cat"] = {
        LoadTexture("assets/cat.png"),
        LoadSound("assets/cat.wav"),
        0.0f, -0.4f,
        -0.95f, 0.85f,
        "CAT",
        true,
        true
    };

    animals["lion"] = {
        LoadTexture("assets/lion.png"),
        LoadSound("assets/lion.wav"),
        0.9f, -0.8f,
        -0.95f, 0.60f,
        "LION",
        false,
        false
    };

    animals["elephant"] = {
        LoadTexture("assets/elephant.png"),
        LoadSound("assets/elephant.wav"),
        -0.5f, 0.35f,
        -0.95f, 0.35f,
        "ELEPHANT",
        false,
        false
    };

    animals["bird"] = {
        LoadTexture("assets/bird.png"),
        LoadSound("assets/bird.wav"),
        0.3f, -0.5f,
        -0.95f, 0.10f,
        "BIRD",
        false,
        false
    };

    animals["dog"] = {
        LoadTexture("assets/dog.png"),
        LoadSound("assets/dog.wav"),
        0.75f, 0.6f,
        -0.95f, -0.15f,
        "DOG",
        false,
        false
    };

    animals["cow"] = {
        LoadTexture("assets/cow.png"),
        LoadSound("assets/cow.wav"),
        -0.5f, -1.0f,
        -0.95f, -0.40f,
        "COW",
        false,
        false
    };

    // Create sound buttons
    const float containerLeft = -0.97f;
    const float containerRight = -0.53f;
    const float containerWidth = containerRight - containerLeft;
    const float playBtnSize = 0.08f;
    const int numButtons = animalOrder.size();
    const float verticalTop = 0.45f;
    const float verticalBottom = -0.85f;
    const float verticalGap = 0.04f;
    const float totalVerticalSpace = verticalTop - verticalBottom;
    const float buttonHeight = (totalVerticalSpace - (numButtons - 1) * verticalGap) / numButtons;
    float currentY = verticalTop;

    for (const auto& animalName : animalOrder) {
        SoundButton sb;
        sb.x = containerLeft;
        sb.y = currentY;
        sb.width = containerWidth;
        sb.height = buttonHeight;
        sb.label = animals[animalName].displayName;
        sb.unlocked = animals[animalName].unlocked;
        sb.color = goldenColor;
        sb.soundBuffer = animals[animalName].soundBuffer;

        // Create sound source for this button
        alGenSources(1, &sb.soundSource);
        alSourcei(sb.soundSource, AL_BUFFER, sb.soundBuffer);
        alSourcef(sb.soundSource, AL_GAIN, 1.0f);
        sb.isPlaying = false;

        sb.playBtnX = sb.x + sb.width - sb.playBtnSize - 0.02f;
        sb.playBtnY = currentY + (buttonHeight - playBtnSize) / 2;
        sb.playBtnSize = playBtnSize;

        sb.lockX = sb.x + (sb.width - playBtnSize) / 2;
        sb.lockY = sb.playBtnY;

        soundButtons.push_back(sb);
        currentY -= buttonHeight + verticalGap;
    }
    // Add this at the end of InitializeAnimals()
    backgroundMusic = LoadSound("assets/music.wav");  // Replace with your music file
    if (backgroundMusic) {
        alGenSources(1, &musicSource);
        alSourcei(musicSource, AL_BUFFER, backgroundMusic);
        alSourcei(musicSource, AL_LOOPING, AL_TRUE);  // Make it loop forever
        alSourcef(musicSource, AL_GAIN, 0.4f); //30% volume
        alSourcePlay(musicSource);  // Start playing immediately
    }
}

void HandleClicks(GLFWwindow* window) {
    static bool debounce = false;

    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS && !debounce) {
        debounce = true;

        double mx, my;
        glfwGetCursorPos(window, &mx, &my);
        int winW, winH;
        glfwGetWindowSize(window, &winW, &winH);
        float normX = static_cast<float>((mx / winW) * 2 - 1);
        float normY = static_cast<float>(1 - (my / winH) * 2);
        //bool clickedSomething = false;

        for (auto& pair : animals) {
            Animal& animal = pair.second;
            if (IsClicked(normX, normY, animal.x, animal.y)) {
                if (animal.unlocked) {
                    animal.isPopping = true;
                    animal.popTimer = 0.0f;

                    // ?? Find the expected current animal (first unlocked and not yet identified)
                    std::string expectedAnimal = "";
                    for (const std::string& name : animalOrder) {
                        if (animals[name].unlocked && animals[name].soundUnlocked && !animals[name].found) {
                            expectedAnimal = name;
                            break;
                        }
                    }

                    if (pair.first == expectedAnimal) {
                        feedbackMessage.text = "CORRECT!";
                        feedbackMessage.color = { 1.0f, 1.0f, 0.0f };
                        animal.found = true;
                        // Unlock the next animal
                        auto it = std::find(animalOrder.begin(), animalOrder.end(), pair.first);
                        if (it != animalOrder.end() && (it + 1) != animalOrder.end()) {
                            animalToUnlock = *(it + 1);
                            pendingUnlock = true;
                            unlockTimer = 2.0f;  // wait 2 seconds
                            std::string nextAnimal = *(it + 1);
                            animals[nextAnimal].unlocked = true;
                            animals[nextAnimal].soundUnlocked = true;

                            // Also unlock sound button
                            for (auto& button : soundButtons) {
                                if (button.label == animals[nextAnimal].displayName) {
                                    button.unlocked = true;
                                    break;
                                }
                            }
                        }
                    }
                    else {
                        feedbackMessage.text = "WRONG!";
                        feedbackMessage.color = { 1.0f, 0.0f, 0.0f };
                    }

                    // Show feedback text
                    // Show "CORRECT" at top center
                    feedbackMessage.x = 0.0f;   // Center horizontally
                    feedbackMessage.y = 0.85f;  // Near top
                    feedbackMessage.timer = 2.0f;

                    // Play the clicked animal sound
                    ALuint tempSource;
                    alGenSources(1, &tempSource);
                    alSourcei(tempSource, AL_BUFFER, animal.soundBuffer);
                    alSourcePlay(tempSource);
                    tempSources.push_back(tempSource);

                    // Play feedback sound (correct or incorrect)
                    ALuint feedbackSource;
                    alGenSources(1, &feedbackSource);
                    alSourcei(feedbackSource, AL_BUFFER,
                        (pair.first == expectedAnimal) ? correctSound : incorrectSound);
                    alSourcePlay(feedbackSource);
                    tempSources.push_back(feedbackSource);
                }
                return;
            }
        }

        // Check sound button clicks
        for (auto& button : soundButtons) {
            if (button.unlocked &&
                normX >= button.playBtnX && normX <= button.playBtnX + button.playBtnSize &&
                normY >= button.playBtnY && normY <= button.playBtnY + button.playBtnSize) {

                ALint state;
                alGetSourcei(button.soundSource, AL_SOURCE_STATE, &state);

                if (state == AL_PLAYING) {
                    alSourceStop(button.soundSource);
                    button.isPlaying = false;
                }
                else {
                    alSourcePlay(button.soundSource);
                    button.isPlaying = true;
                }
                break;
            }
        }
    }
    else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_RELEASE) {
        debounce = false;
    }
}
void UpdateUnlockTimer(float deltaTime) {
    if (pendingUnlock) {
        unlockTimer -= deltaTime;
        if (unlockTimer <= 0.0f) {
            animals[animalToUnlock].unlocked = true;
            animals[animalToUnlock].soundUnlocked = true;

            // Also unlock sound button
            for (auto& button : soundButtons) {
                if (button.label == animals[animalToUnlock].displayName) {
                    button.unlocked = true;
                    break;
                }
            }

            pendingUnlock = false;
            animalToUnlock = "";
        }
    }
}


int main() {
    int argc = 1;
    char* argv[1] = { (char*)"Something" };
    glutInit(&argc, argv);

    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }

    GLFWwindow* window = glfwCreateWindow(1400, 900, "Moo Who?", NULL, NULL);
    if (!window) {
        glfwTerminate();
        return -1;
    }

    // Load and set window icon
        GLFWimage icon = LoadIconImage("assets/iconGame.png");
    if (icon.pixels) {
        glfwSetWindowIcon(window, 1, &icon);
        stbi_image_free(icon.pixels); // Free the memory after setting
    }
    else {
        std::cerr << "Warning: Window icon not loaded" << std::endl;
    }
    std::cout << "Window icon set with image: " << icon.width << "x" << icon.height << std::
endl;

    glfwMakeContextCurrent(window);

    if (glewInit() != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW" << std::endl;
        glfwTerminate();
        return -1;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Initialize OpenAL
    ALCdevice* device = alcOpenDevice(NULL);
    if (!device) {
        std::cerr << "Failed to open OpenAL device" << std::endl;
        glfwTerminate();
        return -1;
    }

    ALCcontext* context = alcCreateContext(device, NULL);
    if (!alcMakeContextCurrent(context)) {
        std::cerr << "Failed to make OpenAL context current" << std::endl;
        alcCloseDevice(device);
        glfwTerminate();
        return -1;
    }

    // Set listener properties
    alListener3f(AL_POSITION, 0.0f, 0.0f, 0.0f);
    alListener3f(AL_VELOCITY, 0.0f, 0.0f, 0.0f);
    ALfloat listenerOri[] = { 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f };
    alListenerfv(AL_ORIENTATION, listenerOri);

    InitializeAnimals();

    float lastTime = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        float currentTime = glfwGetTime();
        float deltaTime = currentTime - lastTime;
        lastTime = currentTime;

        UpdateAnimations(deltaTime);
        UpdateMessages(deltaTime);
        CleanUpTempSources();
        UpdateUnlockTimer(deltaTime);

        glClear(GL_COLOR_BUFFER_BIT);
        glLoadIdentity();

        DrawBackground(backgroundTex);
        DrawSoundboardUI(window);

        for (const auto& pair : animals) {
            const Animal& animal = pair.second;
            DrawAnimal(animal);
        }

        if (feedbackMessage.timer > 0.0f) {
            DrawText(window, feedbackMessage.text, feedbackMessage.x, feedbackMessage.y, feedbackMessage.color);
        }

        HandleClicks(window);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Clean up
    for (auto& button : soundButtons) {
        if (button.soundSource) {
            alDeleteSources(1, &button.soundSource);
        }
    }

    for (auto& pair : animals) {
        if (pair.second.soundBuffer) {
            alDeleteBuffers(1, &pair.second.soundBuffer);
        }
    }

    for (auto source : tempSources) {
        alDeleteSources(1, &source);
    }

    if (correctSound) alDeleteBuffers(1, &correctSound);
    if (incorrectSound) alDeleteBuffers(1, &incorrectSound);

    // Add this before your other OpenAL cleanup code
    alSourceStop(musicSource);
    alDeleteSources(1, &musicSource);
    alDeleteBuffers(1, &backgroundMusic);

    alcMakeContextCurrent(NULL);
    alcDestroyContext(context);
    alcCloseDevice(device);

    glDeleteTextures(1, &playTex);
    glDeleteTextures(1, &pauseTex);
    glDeleteTextures(1, &lockTex);
    glDeleteTextures(1, &soundboardTex);
    glDeleteTextures(1, &backgroundTex);

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}