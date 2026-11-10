// SPDX-License-Identifier: GPL-3.0-or-later
//
// Lanzamiento de la omnillave al estilo de A Crack in Time, para Ratchet & Clank 1.
//
// Con la llave en la mano:
//   - R2 mantenido: Ratchet levanta el brazo, preparando el lanzamiento.
//   - R2 + Cuadrado: lanza la llave hacia donde mira, de pie, corriendo o saltando. Con un arma
//     en la mano, primero cambia a la llave.
//
// Cómo funciona:
//   - El gancho del mando quita R2 y Cuadrado de lo que ve el juego, para que no se agache ni
//     haga el golpe agachado. Sin R2, Cuadrado sigue siendo el combo normal.
//   - El gancho de la actualización del héroe llama a la función del propio juego que lanza la
//     llave (launch_hero_wrench). No cambia su estado, así que sigue andando o corriendo.
//   - El brazo se mueve con hero_set_joints: giros encima de la animación que esté sonando.
//
// Las direcciones del héroe y del mando son las del mod lock-strafe (GPL-3.0, OpenRAC).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <vector>

#include "openrac/games/rac1.hpp"
#include "openrac/mod.hpp"

namespace {

using gaddr = uint32_t;
namespace hooks = openrac::rac1;

struct Disc {
    const char* version;
    gaddr hero;
    gaddr pad;
    gaddr level_mode;     // el modo de juego, 0 jugando (en el programa del nivel 0)
    const char* launch;   // launch_hero_wrench
};

const Disc k_discs[] = {
    {"pal", 0x0013F450u, 0x0013CA40u, 0x0015F6A8u, "func_L00_00217DE0"},
    {"ntsc", 0x0013F350u, 0x0013C940u, 0x0015F5C4u, "FUN_L00_00217658"},
};

// En el héroe (include/structs/hero.h de rac1-decomp).
constexpr uint32_t k_hero_slot0_moby = 0x1090u;
constexpr uint32_t k_hero_slot0_state = 0x10B4u;  // 2 = listo
constexpr uint32_t k_hero_slot0_item = 0x10B8u;
constexpr uint32_t k_hero_state = 0x2084u;
constexpr uint32_t k_hero_control_mode = 0x208Cu;
constexpr uint32_t k_hero_vehicle_mode = 0x20A4u;
constexpr uint32_t k_hero_item_locked = 0x20ACu;
// Pide al juego que cambie el objeto de la mano por la llave (hero.h: force_slot0_item8); es lo
// mismo que hace al pulsar Cuadrado con un arma, y recuerda el arma para volver a ella.
constexpr uint32_t k_hero_force_wrench = 0x20A6u;
constexpr uint32_t k_hero_slip_ground = 0x12EAu;
constexpr int32_t k_item_wrench = 8;

// En un moby: su estado. En la llave: 0 en la mano, 10 volando, 11 volviendo.
constexpr uint32_t k_moby_state = 0x20u;

// En el mando (PadState): las palabras de botones y el historial (como lock-strafe).
constexpr uint32_t k_pad_held = 0x1A0u;
constexpr uint32_t k_pad_pressed = 0x1A4u;
constexpr uint32_t k_pad_words[] =
    {0x1A0u, 0x1A4u, 0x1A8u, 0x1B0u, 0x1B4u, 0x1B8u, 0x1C0u, 0x1C4u, 0x1C8u};
constexpr uint32_t k_pad_history_index = 0x18Eu;
constexpr uint32_t k_pad_history = 0x1E0u;
constexpr int k_pad_history_length = 30;
constexpr uint32_t k_btn_r2 = 0x0002u;
constexpr uint32_t k_btn_square = 0x0080u;

// Los huesos del brazo derecho de Ratchet (los de la mano que lleva la llave).
constexpr uint32_t k_joint_spine = 5;
constexpr uint32_t k_joint_shoulder = 53;
constexpr uint32_t k_joint_elbow = 55;
constexpr uint32_t k_joint_hand = 56;

// La duración en ticks del levantar y del lanzamiento (a 50 ticks/s en PAL).
constexpr float k_raise_ticks = 8.0f;
constexpr float k_throw_ticks = 18.0f;

openrac::Mod* s_mod = nullptr;
const Disc* s_disc = nullptr;
void (*s_launch)() = nullptr;

bool s_aiming = false;         // R2 mantenido, con la llave en la mano y a pie
bool s_throw_pending = false;  // Cuadrado pulsado con R2: se lanza en cuanto se pueda
int s_throw_wait = 0;          // ticks que quedan para esperar a que la llave llegue a la mano
constexpr int k_throw_wait_ticks = 90;  // a lo sumo 1,8 s: si no llega, se olvida
float s_raise = 0.0f;          // 0 brazo normal, 1 brazo levantado
int s_throw_tick = -1;         // ticks desde el lanzamiento, -1 sin lanzamiento
bool s_shown = false;          // hay huesos enviados al héroe

template <class T>
T& ref(gaddr at) {
    return s_mod->memory().ref<T>(at);
}

gaddr hero() {
    return s_disc->hero;
}

bool playing() {
    return s_mod->loaded_level() >= 0
           && ref<int32_t>(s_mod->relocate_level(0, s_disc->level_mode)) == 0;
}

// A pie, con algo en la mano (la llave, un arma o un artilugio).
bool on_foot() {
    return playing() && ref<uint8_t>(hero() + k_hero_vehicle_mode) == 0
           && ref<uint32_t>(hero() + k_hero_slot0_moby) != 0;
}

// A pie, lleve lo que lleve (aunque la mano esté vacía un instante al cambiar de objeto).
bool on_foot_any() {
    return playing() && ref<uint8_t>(hero() + k_hero_vehicle_mode) == 0;
}

// Una línea en el registro cuando un lanzamiento no sale, para entender el fallo (40 como mucho).
int s_diag_lines = 0;
void diag(const char* what) {
    if (s_diag_lines >= 40) {
        return;
    }
    ++s_diag_lines;
    const gaddr moby = ref<uint32_t>(hero() + k_hero_slot0_moby);
    char text[200];
    std::snprintf(text, sizeof text,
                  "%s: objeto %d, listo %d, estado 0x%X, modo %d, objeto de la mano %s, bloqueo %d, resbala %d",
                  what, ref<int32_t>(hero() + k_hero_slot0_item), ref<int32_t>(hero() + k_hero_slot0_state),
                  ref<int32_t>(hero() + k_hero_state), ref<int32_t>(hero() + k_hero_control_mode),
                  moby != 0 ? (ref<uint8_t>(moby + k_moby_state) == 0 ? "quieto" : "ocupado") : "ninguno",
                  ref<uint8_t>(hero() + k_hero_item_locked), ref<uint8_t>(hero() + k_hero_slip_ground));
    s_mod->info(text);
}

bool on_foot_with_wrench() {
    return on_foot() && ref<int32_t>(hero() + k_hero_slot0_item) == k_item_wrench;
}

// En el aire: los mismos estados en los que el juego deja golpear con la llave en un salto
// (hero_state.h de rac1-decomp), más los saltos con la mochila.
bool is_airborne_state(int32_t state) {
    switch (state) {
        case 0x06:  // cayendo
        case 0x07:  // saltando
        case 0x08:  // aterrizando
        case 0x09:  // salto corriendo
        case 0x0A:  // salto con la mochila
        case 0x0B:  // salto alto
        case 0x0E:  // doble salto
        case 0x10:  // salto largo con la mochila
        case 0x11:
        case 0x2D:  // cayendo al soltar un objeto
            return true;
        default:
            return false;
    }
}

// Se puede lanzar: de pie, corriendo o saltando, con la llave en la mano y sin otra acción que
// lo impida.
bool can_throw() {
    const gaddr moby = ref<uint32_t>(hero() + k_hero_slot0_moby);
    const int32_t mode = ref<int32_t>(hero() + k_hero_control_mode);
    const bool grounded = mode >= 0 && mode < 2 && ref<uint8_t>(hero() + k_hero_slip_ground) == 0;
    return on_foot_with_wrench() && (grounded || is_airborne_state(ref<int32_t>(hero() + k_hero_state)))
           && ref<int32_t>(hero() + k_hero_slot0_state) == 2
           && ref<uint8_t>(hero() + k_hero_item_locked) == 0 && ref<uint8_t>(moby + k_moby_state) == 0;
}

// Quita botones de todo lo que el juego lee del mando en este sondeo.
void hide_buttons(uint32_t mask) {
    const gaddr pad = s_disc->pad;
    for (const uint32_t word : k_pad_words) {
        ref<uint32_t>(pad + word) &= ~mask;
    }
    const int last = (ref<int16_t>(pad + k_pad_history_index) + (k_pad_history_length - 1))
                     % k_pad_history_length;
    ref<uint32_t>(pad + k_pad_history + 4u * static_cast<unsigned>(last)) &= ~mask;
}

template <class Original>
void pad_process_report(Original original, gaddr pad_state, gaddr report, int32_t size) {
    original(pad_state, report, size);
    if (pad_state != s_disc->pad) {
        return;
    }
    s_aiming = false;
    if (!on_foot_any()) {
        return;
    }
    const uint32_t held = ref<uint32_t>(pad_state + k_pad_held);
    const uint32_t pressed = ref<uint32_t>(pad_state + k_pad_pressed);
    if ((held & k_btn_r2) == 0) {
        // Sin apuntar, solo se oculta R2 si sigue apuntando el juego: nada que hacer.
        return;
    }
    // El brazo solo se levanta con la llave en la mano; con un arma, R2 + Cuadrado la saca y la lanza.
    s_aiming = on_foot_with_wrench();
    if ((pressed & k_btn_square) != 0) {
        s_throw_pending = true;
        s_throw_wait = k_throw_wait_ticks;
    }
    hide_buttons(k_btn_r2 | k_btn_square);
}

openrac_joint_offset joint_rotation(uint32_t index, float x, float y, float z) {
    // Giros pequeños en orden x, y, z, en radianes, en el marco propio del hueso.
    const float cx = std::cos(x * 0.5f), sx = std::sin(x * 0.5f);
    const float cy = std::cos(y * 0.5f), sy = std::sin(y * 0.5f);
    const float cz = std::cos(z * 0.5f), sz = std::sin(z * 0.5f);
    openrac_joint_offset j{};
    j.joint = index;
    j.rotation[0] = sx * cy * cz + cx * sy * sz;
    j.rotation[1] = cx * sy * cz - sx * cy * sz;
    j.rotation[2] = cx * cy * sz + sx * sy * cz;
    j.rotation[3] = cx * cy * cz - sx * sy * sz;
    return j;
}

float env_float(const char* name, float fallback) {
    const char* text = std::getenv(name);
    return text != nullptr ? static_cast<float>(std::atof(text)) : fallback;
}

// Los ángulos del brazo. Se pueden ajustar sin compilar con WT_* para probar poses.
struct Angles {
    float raise_x, raise_y, raise_z, elbow_raise;
    float swing_x, swing_y, swing_z, elbow_swing, spine_swing;
    float hand_x, hand_y, hand_z;  // la muñeca al levantar el brazo: la llave en vertical
};
Angles s_angles;

void send_pose() {
    // La curva del lanzamiento: sube hasta el 30 % y luego baja suave.
    float swing = 0.0f;
    if (s_throw_tick >= 0) {
        const float t = static_cast<float>(s_throw_tick) / k_throw_ticks;
        swing = t < 0.3f ? t / 0.3f : 1.0f - (t - 0.3f) / 0.7f;
        swing = swing * swing * (3.0f - 2.0f * swing);
    }
    const float r = s_raise * s_raise * (3.0f - 2.0f * s_raise);
    if (r <= 0.001f && swing <= 0.001f) {
        if (s_shown) {
            s_mod->hero_set_joints({});
            s_shown = false;
        }
        return;
    }
    const Angles& a = s_angles;
    std::vector<openrac_joint_offset> joints;
    joints.push_back(joint_rotation(
        k_joint_shoulder,
        a.raise_x * r + a.swing_x * swing,
        a.raise_y * r + a.swing_y * swing,
        a.raise_z * r + a.swing_z * swing
    ));
    joints.push_back(joint_rotation(k_joint_elbow, 0.0f, 0.0f, a.elbow_raise * r + a.elbow_swing * swing));
    // Al lanzar, la muñeca vuelve a su sitio para que el brazo se estire natural.
    const float wrist = r * (1.0f - swing);
    joints.push_back(joint_rotation(k_joint_hand, a.hand_x * wrist, a.hand_y * wrist, a.hand_z * wrist));
    if (swing > 0.001f) {
        joints.push_back(joint_rotation(k_joint_spine, 0.0f, 0.0f, a.spine_swing * swing));
    }
    s_shown = s_mod->hero_set_joints(joints) || s_shown;
}

template <class Original>
void hero_update(Original original) {
    original();
    if (s_throw_pending) {
        if (can_throw()) {
            s_throw_pending = false;
            s_launch();
            s_throw_tick = 0;
        } else if (--s_throw_wait <= 0 || !on_foot_any()) {
            s_throw_pending = false;
            diag("no lanzada");
        } else if (!on_foot_with_wrench()) {
            // Con un arma: el juego cambia a la llave y se lanza cuando ya está en la mano.
            ref<uint8_t>(hero() + k_hero_force_wrench) = 1;
        }
    }
    const float step = 1.0f / k_raise_ticks;
    s_raise += s_aiming ? step : -step;
    s_raise = s_raise < 0.0f ? 0.0f : (s_raise > 1.0f ? 1.0f : s_raise);
    if (s_throw_tick >= 0 && static_cast<float>(++s_throw_tick) > k_throw_ticks) {
        s_throw_tick = -1;
    }
    send_pose();
}

void load(openrac::Mod& mod) {
    if (mod.game() != "rac1") {
        throw openrac::Error("este mod es para Ratchet & Clank");
    }
    s_mod = &mod;
    for (const auto& disc : k_discs) {
        if (mod.version() == disc.version) {
            s_disc = &disc;
        }
    }
    if (s_disc == nullptr) {
        throw openrac::Error("disco no compatible: " + std::string(mod.version()));
    }
    if (!mod.has_api_12() || mod.api().hero_set_joints == nullptr) {
        throw openrac::Error("hace falta un OpenRAC con hero_set_joints (1.12)");
    }
    s_launch = mod.required_function<void (*)()>(s_disc->launch);

    s_angles = Angles{
        env_float("WT_RAISE_X", 0.7f), env_float("WT_RAISE_Y", 0.0f),  env_float("WT_RAISE_Z", -0.5f),
        env_float("WT_ELBOW_RAISE", -1.7f), env_float("WT_SWING_X", -0.3f), env_float("WT_SWING_Y", 0.0f),
        env_float("WT_SWING_Z", -0.9f),    env_float("WT_ELBOW_SWING", 1.5f), env_float("WT_SPINE_SWING", 0.35f),
        env_float("WT_HAND_X", 1.57f),     env_float("WT_HAND_Y", 0.0f),      env_float("WT_HAND_Z", 0.0f),
    };

    mod.hook<hooks::Pad_ProcessReport>([](auto original, uint32_t pad, uint32_t report, int32_t size) {
        pad_process_report(original, pad, report, size);
    });
    mod.hook<hooks::Hero_Update>([](auto original) { hero_update(original); });
    mod.info("lanzamiento de llave listo en el disco " + std::string(mod.version()));
}

}  // namespace

OPENRAC_MOD_CPP("wrench-throw", "1.0.2", load)
