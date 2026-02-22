<p align="center">
    <img width="400" height="160" src="https://live.staticflickr.com/65535/49908747533_f359f83610_w.jpg">
    <br>
    <a href="https://github.com/brainflow-dev/brainflow/releases">
       <img alt="GitHub all releases" src="https://img.shields.io/github/downloads/brainflow-dev/brainflow/total?color=yellow&label=Downloads%28Github%29">
    </a>
    <a href="https://pypi.org/project/brainflow/">
        <img alt="PYPI" src="https://static.pepy.tech/personalized-badge/brainflow?period=total&units=international_system&left_color=grey&right_color=yellow&left_text=Downloads(PYPI)">
    </a>
    <a href="https://www.nuget.org/packages/brainflow/">
        <img alt="Nuget" src="https://img.shields.io/nuget/dt/brainflow?color=yellow&label=Downloads%28Nuget%29&logo=BrainFlow">
    </a>
</p>

BrainFlow is a library intended to obtain, parse and analyze EEG, EMG, ECG, and other kinds of data from biosensors.

It provides a uniform SDK to work with biosensors with a primary focus on neurointerfaces, all features available for free and distributed under MIT license.

---

The following documentation was **modified** to focus on instructions for boards from NeuroPawn. 

To look at the original docs, click [here](https://github.com/brainflow-dev/brainflow)

## NeuroPawn Knight Board

### Brainflow Setup Example (Python)

```
import brainflow as bf
import time

from brainflow.board_shim import BoardShim, BrainFlowInputParams, BoardIds

class KnightBoard:
    def __init__(self, serial_port: str, num_channels: int):
        """Initialize and configure the Knight Board."""
        self.params = BrainFlowInputParams()
        self.params.serial_port = serial_port
        # optional: set gain override (default 12)
        self.params.other_info = '{"gain": 6}'
        self.num_channels = num_channels

        # Initialize board
        self.board_shim = BoardShim(BoardIds.NEUROPAWN_KNIGHT_BOARD.value, self.params)
        self.board_id = self.board_shim.get_board_id()
        self.eeg_channels = self.board_shim.get_exg_channels(self.board_id)
        self.sampling_rate = self.board_shim.get_sampling_rate(self.board_id)

    def start_stream(self, buffer_size: int = 450000):
        """Start the data stream from the board."""
        self.board_shim.prepare_session()
        self.board_shim.start_stream(buffer_size)
        print("Stream started.")
        time.sleep(2)
        for x in range(1, self.num_channels + 1):
            time.sleep(0.5)
            cmd = f"chon_{x}_12"
            self.board_shim.config_board(cmd)
            print(f"sending {cmd}")
            time.sleep(1)
            rld = f"rldadd_{x}"
            self.board_shim.config_board(rld)
            print(f"sending {rld}")
            time.sleep(0.5)

    def stop_stream(self):
        """Stop the data stream and release resources."""
        self.board_shim.stop_stream()
        self.board_shim.release_session()
        print("Stream stopped and session released.")

Knight_board = KnightBoard("COM3", 8)
Knight_board.start_stream()

while True:
    data = Knight_board.board_shim.get_board_data()
    # do stuff with data

    if keyboard.is_pressed('q'):
        Knight_board.stop_stream()
        break
```

### BrainFlow Input Parameters

To configure the NeuroPawn Knight board with BrainFlow, pass the parameters as strings into:

    params.other_info = <parameter>

#### Parameter 1: Set Global Gain

**Purpose**: Sets the global gain for the board, used for scaling EEG data. This must match the gain set in the firmware (default 12).

    params.other_info = '{'gain': 6}'

##### Parameters:

- gain: Specifies the gain value for the entire board. Allowable gain values are: [1, 2, 3, 4, 6, 8, 12 (recommended)].


### BrainFlow Configuration Commands

To configure the NeuroPawn Knight board with BrainFlow, pass the commands as strings into:
    
    board_shim.config_board(<command>)

#### Command 1: Enable EEG Channel / Set Gain
**Purpose**: Enables a specified channel with a specified gain, starting data acquisition on that channel. If the channel is already enabled, it will remain enabled, but will still update its gain.

    f"chon_{channel}_{gain_value}"

##### Parameters:

- channel: The channel number to start the data acquisition. Replace this with the actual number of the channel you want to configure. One-indexed.

- gain: Specifies the gain value for the channel to be enabled. Allowable gain values are: [1, 2, 3, 4, 6, 8, 12 (recommended)]. The gain value controls the amplification level of the EEG signal on the specified channel.

#### Command 2: Disable EEG Channel
**Purpose**: Disables a specified channel, stopping data acquisition on that channel.

    f"choff_{channel_number}"

##### Parameters:

- channel_number: The channel number to **stop** the data acquisition. This is appended to *'choff'* to construct the configuration command. One-indexed.
  
#### Command 3: Toggle on RLD
**Purpose**: Toogle **on** right leg drive for the specified channel.

    f"rldadd_{channel_number}"

##### Parameters:

- channel_number: The channel number to toggle **on** the right leg drive. This number is converted to a string and appended to *'rldadd'* to create the configuration command. One-indexed.


### Command 4: Toggle off RLD
**Purpose**: Toogle **off** right leg drive for the specified channel.

    f"rldremove_{channel}"

#### Parameters:

- channel_number: The channel number to toggle **off** the right leg drive. This number is converted to a string and appended to *'rldremove'* to create the configuration command. One-indexed.

## NeuroPawn Knight IMU Board

### Starter Example (Python)

```python
# Insert your Python script here
```

