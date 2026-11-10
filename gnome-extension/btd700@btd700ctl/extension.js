// Quick Settings toggle for the Sennheiser BTD 700 dongle.
//
// The dongle runs its own Bluetooth stack, so GNOME's Bluetooth menu never
// sees it. This extension talks to it through the `btd700ctl` CLI instead:
// `btd700ctl monitor` streams state changes, `btd700ctl connect|disconnect`
// is run when the toggle is clicked.

import GObject from 'gi://GObject';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';

import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import {QuickToggle, SystemIndicator} from 'resource:///org/gnome/shell/ui/quickSettings.js';

const ICON = 'audio-headphones-symbolic';
const RESTART_DELAY_S = 3;

const SUBTITLES = {
    'none': 'Idle',
    'disconnected': 'Disconnected',
    'connected': 'Connected',
    'streaming-audio': 'Playing',
    'streaming-voice': 'In call',
};

const LINKED = new Set(['connected', 'streaming-audio', 'streaming-voice']);

function findCli() {
    return GLib.find_program_in_path('btd700ctl') ??
        ['/usr/local/bin/btd700ctl', '/usr/bin/btd700ctl']
            .find(p => GLib.file_test(p, GLib.FileTest.IS_EXECUTABLE)) ??
        null;
}

const Btd700Toggle = GObject.registerClass(
class Btd700Toggle extends QuickToggle {
    constructor(cli) {
        super({
            title: 'BTD 700',
            iconName: ICON,
            toggleMode: false,
        });
        this._cli = cli;
        this._state = 'absent';
        this._pending = null;
        this.connect('clicked', () => this._onClicked());
    }

    setState(state) {
        this._state = state;
        // a state change event means the dongle reacted to our request
        if (this._pending && (LINKED.has(state) === (this._pending === 'connect')))
            this._pending = null;
        this._sync();
    }

    _sync() {
        const present = this._state !== 'absent';
        this.visible = present;
        this.checked = LINKED.has(this._state);
        if (this._pending === 'connect')
            this.subtitle = 'Connecting…';
        else if (this._pending === 'disconnect')
            this.subtitle = 'Disconnecting…';
        else
            this.subtitle = SUBTITLES[this._state] ?? this._state;
    }

    _onClicked() {
        const action = LINKED.has(this._state) ? 'disconnect' : 'connect';
        this._pending = action;
        this._sync();

        try {
            const proc = Gio.Subprocess.new([this._cli, action],
                Gio.SubprocessFlags.STDOUT_SILENCE | Gio.SubprocessFlags.STDERR_PIPE);
            proc.communicate_utf8_async(null, null, (p, res) => {
                let ok = false;
                let stderr = '';
                try {
                    [, , stderr] = p.communicate_utf8_finish(res);
                    ok = p.get_successful();
                } catch (e) {
                    stderr = e.message;
                }
                if (!ok) {
                    this._pending = null;
                    this._sync();
                    Main.notifyError('BTD 700', `Could not ${action}: ${(stderr ?? '').trim()}`);
                    return;
                }
                // the headset may take a while (or refuse); clear the hint eventually
                if (this._pendingTimeout)
                    GLib.source_remove(this._pendingTimeout);
                this._pendingTimeout = GLib.timeout_add_seconds(GLib.PRIORITY_DEFAULT, 15, () => {
                    this._pendingTimeout = 0;
                    if (this._pending === action) {
                        this._pending = null;
                        this._sync();
                    }
                    return GLib.SOURCE_REMOVE;
                });
            });
        } catch (e) {
            this._pending = null;
            this._sync();
            Main.notifyError('BTD 700', e.message);
        }
    }

    destroy() {
        if (this._pendingTimeout) {
            GLib.source_remove(this._pendingTimeout);
            this._pendingTimeout = 0;
        }
        super.destroy();
    }
});

const Btd700Indicator = GObject.registerClass(
class Btd700Indicator extends SystemIndicator {
    constructor(cli) {
        super();
        this._cli = cli;

        this._indicator = this._addIndicator();
        this._indicator.iconName = ICON;
        this._indicator.visible = false;

        this._toggle = new Btd700Toggle(cli);
        this._toggle.visible = false;
        this.quickSettingsItems.push(this._toggle);

        this._cancellable = new Gio.Cancellable();
        this._proc = null;
        this._restartId = 0;
        this._startMonitor();
    }

    _setState(state) {
        this._toggle.setState(state);
        // show a headphones icon in the top bar while the headset is on the dongle
        this._indicator.visible = LINKED.has(state);
    }

    _startMonitor() {
        try {
            this._proc = Gio.Subprocess.new([this._cli, 'monitor'],
                Gio.SubprocessFlags.STDOUT_PIPE | Gio.SubprocessFlags.STDERR_SILENCE);
        } catch (e) {
            logError(e, 'btd700: failed to start monitor');
            this._scheduleRestart();
            return;
        }

        const stream = new Gio.DataInputStream({
            base_stream: this._proc.get_stdout_pipe(),
            close_base_stream: true,
        });
        this._readLine(stream);
    }

    _readLine(stream) {
        stream.read_line_async(GLib.PRIORITY_DEFAULT, this._cancellable, (s, res) => {
            let line;
            try {
                [line] = s.read_line_finish_utf8(res);
            } catch (e) {
                if (!e.matches(Gio.IOErrorEnum, Gio.IOErrorEnum.CANCELLED))
                    logError(e, 'btd700: monitor read failed');
                line = null;
                if (this._cancellable.is_cancelled())
                    return;
            }

            if (line === null) {
                // monitor exited; mark absent and restart it
                this._setState('absent');
                this._proc = null;
                this._scheduleRestart();
                return;
            }

            const state = line.trim();
            if (state)
                this._setState(state);
            this._readLine(stream);
        });
    }

    _scheduleRestart() {
        if (this._restartId || this._cancellable.is_cancelled())
            return;
        this._restartId = GLib.timeout_add_seconds(GLib.PRIORITY_DEFAULT, RESTART_DELAY_S, () => {
            this._restartId = 0;
            this._startMonitor();
            return GLib.SOURCE_REMOVE;
        });
    }

    destroy() {
        this._cancellable.cancel();
        if (this._restartId) {
            GLib.source_remove(this._restartId);
            this._restartId = 0;
        }
        this._proc?.force_exit();
        this._proc = null;
        this.quickSettingsItems.forEach(item => item.destroy());
        super.destroy();
    }
});

export default class Btd700Extension extends Extension {
    enable() {
        const cli = findCli();
        if (!cli) {
            Main.notifyError('BTD 700', 'btd700ctl was not found in PATH. Install btd700ctl first.');
            return;
        }
        this._indicator = new Btd700Indicator(cli);
        Main.panel.statusArea.quickSettings.addExternalIndicator(this._indicator);
    }

    disable() {
        this._indicator?.destroy();
        this._indicator = null;
    }
}
