from pathlib import Path

from fafrl.faf_repy import Command, CommandType, FafEnv


def main():
    env = FafEnv(
        "/run/media/eric/Gaming/.faforever/bin",
        Path("python/fafrl/flat_40x40_config.lua").read_text(),
    )
    env.reset()

    move_cmd = Command()
    move_cmd.army_id = 0
    move_cmd.command_type = CommandType.Move
    move_cmd.target_type = 2  # Ground
    move_cmd.clear_queue = 0
    move_cmd.target_x = 10
    move_cmd.target_y = 10.0
    move_cmd.target_z = 30
    move_cmd.unit_ids = [0]

    for i in range(10000):
        print(i)
        if env.is_game_over():
            break
        env.step([move_cmd])

if __name__ == "__main__":
    main()
