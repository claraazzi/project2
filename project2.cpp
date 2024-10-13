#define GL_SILENCE_DEPRECATION
#define STB_IMAGE_IMPLEMENTATION
#include <SDL.h>
#include <OpenGL/gl3.h>
#include <OpenGL/gl3ext.h>
#include "glm/mat4x4.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "ShaderProgram.h"
#include "stb_image.h"
#include <cmath>

#define LOG(argument) std::cout << argument << '\n'

enum AppStatus { RUNNING, TERMINATED };

constexpr int WINDOW_WIDTH  = 640 * 2;
constexpr int WINDOW_HEIGHT = 480 * 2;

constexpr float BG_RED     = 0.9765625f,
                BG_GREEN   = 0.97265625f,
                BG_BLUE    = 0.9609375f,
                BG_OPACITY = 1.0f;

constexpr int VIEWPORT_X      = 0,
              VIEWPORT_Y      = 0,
              VIEWPORT_WIDTH  = WINDOW_WIDTH,
              VIEWPORT_HEIGHT = WINDOW_HEIGHT;

constexpr char V_SHADER_PATH[] = "shaders/vertex_textured.glsl",
               F_SHADER_PATH[] = "shaders/fragment_textured.glsl";

SDL_Window* g_display_window;
AppStatus g_app_status = RUNNING;
ShaderProgram g_shader_program;

glm::mat4 g_view_matrix, g_left_paddle_matrix, g_right_paddle_matrix, g_ball_matrix, g_projection_matrix, g_background_matrix;

float g_previous_ticks = 0.0f;

GLuint g_paddle_texture_id, g_ball_texture_id, g_background_texture_id, g_win_message_texture_id, g_loser_message_texture_id;

float g_left_paddle_y = 0.0f;
float g_right_paddle_y = 0.0f;
float g_ball_x_velocity = 2.5f;
float g_ball_y_velocity = 2.0f;
glm::vec3 ball_position;

bool g_right_paddle_auto = false;
bool g_game_over = false;
int g_player_won = 0; // 0 = no winner, 1 = left player won, 2 = right player won

GLuint load_texture(const char* filepath) {
    int width, height, number_of_components;
    unsigned char* image = stbi_load(filepath, &width, &height, &number_of_components, STBI_rgb_alpha);

    if (image == NULL) {
        LOG("Unable to load image. Make sure the path is correct.");
        assert(false);
    }

    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    stbi_image_free(image);

    return textureID;
}

void initialise() {
    SDL_Init(SDL_INIT_VIDEO);
    g_display_window = SDL_CreateWindow("Paddle Game",
                                      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                      WINDOW_WIDTH, WINDOW_HEIGHT,
                                      SDL_WINDOW_OPENGL);
    SDL_GLContext context = SDL_GL_CreateContext(g_display_window);
    SDL_GL_MakeCurrent(g_display_window, context);

    glViewport(VIEWPORT_X, VIEWPORT_Y, VIEWPORT_WIDTH, VIEWPORT_HEIGHT);

    g_shader_program.load(V_SHADER_PATH, F_SHADER_PATH);

    g_left_paddle_matrix = glm::mat4(1.0f);
    g_right_paddle_matrix = glm::mat4(1.0f);
    g_ball_matrix = glm::mat4(1.0f);  // Initialize the ball matrix
    g_background_matrix = glm::mat4(1.0f);  // Initialize the background matrix

    g_view_matrix = glm::mat4(1.0f);
    g_projection_matrix = glm::ortho(-1.777f, 1.777f, -1.0f, 1.0f, -1.0f, 1.0f);

    g_shader_program.set_projection_matrix(g_projection_matrix);
    g_shader_program.set_view_matrix(g_view_matrix);

    glUseProgram(g_shader_program.get_program_id());

    glClearColor(BG_RED, BG_GREEN, BG_BLUE, BG_OPACITY);

    // Load textures
    g_paddle_texture_id = load_texture("textures/paddle.png");
    g_ball_texture_id = load_texture("textures/ball.png");
    g_background_texture_id = load_texture("textures/court.png");  // Load background
    g_win_message_texture_id = load_texture("textures/win_message.png");
    g_loser_message_texture_id = load_texture("textures/loser.png");  // Load loser message

    // Set initial ball position (in the middle) with some scaling
    g_ball_matrix = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f)), glm::vec3(0.2f, 0.2f, 1.0f));

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

bool check_collision(glm::vec3 ball_pos, glm::vec3 paddle_pos, float paddle_width, float paddle_height, float ball_radius) {
    float paddle_half_width = paddle_width / 2.0f;
    float paddle_half_height = paddle_height / 2.0f;

    bool collision_x = ball_pos.x + ball_radius >= paddle_pos.x - paddle_half_width &&
                       ball_pos.x - ball_radius <= paddle_pos.x + paddle_half_width;

    bool collision_y = ball_pos.y + ball_radius >= paddle_pos.y - paddle_half_height &&
                       ball_pos.y - ball_radius <= paddle_pos.y + paddle_half_height;

    return collision_x && collision_y;
}

void process_input() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT || event.type == SDL_WINDOWEVENT_CLOSE) {
            g_app_status = TERMINATED;
        }

        // Handle discrete key events for toggle
        if (event.type == SDL_KEYDOWN) {
            if (event.key.keysym.sym == SDLK_t) {
                g_right_paddle_auto = !g_right_paddle_auto;  // Toggle the auto mode for the right paddle
            }
        }
    }

    const Uint8* keys = SDL_GetKeyboardState(NULL);

    // Player 1 (left paddle) movement: W and S keys
    if (keys[SDL_SCANCODE_W]) {
        g_left_paddle_y += 0.05f;  // Move up with smooth motion
    }
    if (keys[SDL_SCANCODE_S]) {
        g_left_paddle_y -= 0.05f;  // Move down with smooth motion
    }

    // Player 2 (right paddle) movement: UP and DOWN keys, but only if not in auto mode
    if (!g_right_paddle_auto) {
        if (keys[SDL_SCANCODE_UP]) {
            g_right_paddle_y += 0.05f;  // Move up with smooth motion
        }
        if (keys[SDL_SCANCODE_DOWN]) {
            g_right_paddle_y -= 0.05f;  // Move down with smooth motion
        }
    }

    // Clamp paddle positions to screen boundaries
    g_left_paddle_y = glm::clamp(g_left_paddle_y, -0.75f, 0.75f);
    g_right_paddle_y = glm::clamp(g_right_paddle_y, -0.75f, 0.75f);
}

void update() {
    float ticks = (float)SDL_GetTicks() / 1000.0f;
    float delta_time = ticks - g_previous_ticks;
    g_previous_ticks = ticks;

    if (g_game_over) return;

    // Update paddle positions
    g_left_paddle_matrix = glm::translate(glm::mat4(1.0f), glm::vec3(-1.6f, g_left_paddle_y, 0.0f));
    g_right_paddle_matrix = glm::translate(glm::mat4(1.0f), glm::vec3(1.6f, g_right_paddle_y, 0.0f));

    // Ball movement update
    g_ball_matrix = glm::translate(g_ball_matrix, glm::vec3(g_ball_x_velocity * delta_time, g_ball_y_velocity * delta_time, 0.0f));
    ball_position = glm::vec3(g_ball_matrix[3]);

    // Bounce the ball off the top and bottom walls
    if (ball_position.y + 0.1f >= 1.0f || ball_position.y - 0.1f <= -1.0f) {
        g_ball_y_velocity = -g_ball_y_velocity;  // Reverse Y velocity
    }

    // Ball collision with paddles
    glm::vec3 left_paddle_pos = glm::vec3(g_left_paddle_matrix[3]);
    glm::vec3 right_paddle_pos = glm::vec3(g_right_paddle_matrix[3]);

    if (check_collision(ball_position, left_paddle_pos, 0.2f, 1.0f, 0.1f)) {
        g_ball_x_velocity = fabs(g_ball_x_velocity) * 1.2f;  // Increase speed by 20%
    }
    if (check_collision(ball_position, right_paddle_pos, 0.2f, 1.0f, 0.1f)) {
        g_ball_x_velocity = -fabs(g_ball_x_velocity) * 1.2f;  // Increase speed by 20%
    }

    // Game over condition
    if (ball_position.x <= -1.777f) {
        g_game_over = true;
        g_player_won = 2;  // Right paddle (Player 2) wins
        LOG("Player 2 Wins");
    } else if (ball_position.x >= 1.777f) {
        g_game_over = true;
        g_player_won = 1;  // Left paddle (Player 1) wins
        LOG("Player 1 Wins");
    }

    // Automatic mode for right paddle: up and down motion
    if (g_right_paddle_auto) {
        g_right_paddle_y += 0.03f * sin(SDL_GetTicks() / 1000.0f);  // Smooth automatic motion
        g_right_paddle_y = glm::clamp(g_right_paddle_y, -0.75f, 0.75f);  // Keep paddle in bounds
    }
}

void draw_object(glm::mat4 &object_matrix, GLuint &object_texture_id) {
    g_shader_program.set_model_matrix(object_matrix);
    glBindTexture(GL_TEXTURE_2D, object_texture_id);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void render() {
    glClear(GL_COLOR_BUFFER_BIT);

    float vertices[] = {
        -0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f,
        -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f
    };

    float texture_coordinates[] = {
        0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f,
        0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f
    };

    glVertexAttribPointer(g_shader_program.get_position_attribute(), 2, GL_FLOAT, false, 0, vertices);
    glEnableVertexAttribArray(g_shader_program.get_position_attribute());

    glVertexAttribPointer(g_shader_program.get_tex_coordinate_attribute(), 2, GL_FLOAT, false, 0, texture_coordinates);
    glEnableVertexAttribArray(g_shader_program.get_tex_coordinate_attribute());

    // Draw the background scaled to the full screen
    glm::mat4 background_matrix = glm::scale(glm::mat4(1.0f), glm::vec3(3.554f, 2.0f, 1.0f));  // 3.554f = 1.777 * 2 (to fit screen width), 2.0f to fit height
    draw_object(background_matrix, g_background_texture_id);  // Ensure the background covers the entire screen

    if (!g_game_over) {
        // Draw ball and paddles
        draw_object(g_left_paddle_matrix, g_paddle_texture_id);
        draw_object(g_right_paddle_matrix, g_paddle_texture_id);
        draw_object(g_ball_matrix, g_ball_texture_id);  // Ensure the ball is drawn
    } else {
        // Display win or lose message
        if (g_player_won == 1) {
            glm::mat4 win_message_matrix = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f)), glm::vec3(1.0f, 1.0f, 1.0f));
            draw_object(win_message_matrix, g_win_message_texture_id);  // Player 1 wins, draw win message
        } else if (g_player_won == 2) {
            glm::mat4 lose_message_matrix = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f)), glm::vec3(1.0f, 1.0f, 1.0f));
            draw_object(lose_message_matrix, g_loser_message_texture_id);  // Player 2 wins, draw lose message
        }
    }

    glDisableVertexAttribArray(g_shader_program.get_position_attribute());
    glDisableVertexAttribArray(g_shader_program.get_tex_coordinate_attribute());

    SDL_GL_SwapWindow(g_display_window);
}

void shutdown() {
    SDL_Quit();
}

int main(int argc, char* argv[]) {
    initialise();

    while (g_app_status == RUNNING) {
        process_input();
        update();
        render();
    }

    shutdown();
    return 0;
}





