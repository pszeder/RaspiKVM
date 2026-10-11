<template>
  <section class="tv-settings" aria-label="TV control">
    <h2>TV control</h2>
    <p>Follow the selected computer on your LG webOS TV and wake the TV when you use the keyboard or mouse.</p>
    <p v-if="!settings" role="status">{{ error || 'Loading TV settings…' }}</p>
    <template v-else>
      <label class="check"><input type="checkbox" v-model="settings.enabled" :disabled="busy" @change="changed"> Enable direct LG TV control</label>
      <fieldset :disabled="busy">
        <legend>TV connection</legend>
        <label for="tv-address">TV IP address</label>
        <input id="tv-address" type="text" v-model.trim="settings.host" placeholder="192.168.1.192" autocomplete="off" @input="changed">
        <label for="tv-mac">TV network MAC address</label>
        <input id="tv-mac" type="text" v-model.trim="settings.mac" placeholder="AA:BB:CC:DD:EE:FF" autocomplete="off" @input="changed">
        <p class="help">Use the address of the TV’s active network connection. This is used to wake the TV.</p>
        <div class="inputs">
          <label for="tv-usb">USB / PC input<select id="tv-usb" v-model="settings.usbInput" @change="changed"><option v-for="input in inputs" :key="input.id" :value="input.id">{{ input.label }}</option></select></label>
          <label for="tv-bluetooth">Bluetooth / Mac input<select id="tv-bluetooth" v-model="settings.bluetoothInput" @change="changed"><option v-for="input in inputs" :key="input.id" :value="input.id">{{ input.label }}</option></select></label>
        </div>
        <label class="check"><input type="checkbox" v-model="settings.automaticSwitch" @change="changed"> Change TV input when switching computers</label>
        <label class="check"><input type="checkbox" v-model="settings.wakeOnInput" @change="changed"> Wake TV from keyboard, mouse buttons or scrolling</label>
        <label class="check"><input type="checkbox" v-model="settings.wakeOnMouseMovement" :disabled="!settings.wakeOnInput" @change="changed"> Also wake from mouse movement</label>
        <details><summary>Wake destination</summary><label for="tv-wake-address">Additional wake address</label><input id="tv-wake-address" type="text" v-model.trim="settings.wolDestination" placeholder="192.168.1.255" autocomplete="off" @input="changed"><p class="help">Optional broadcast address for the TV’s network. A routed or shared USB connection may need a wake relay.</p></details>
      </fieldset>
      <button class="save" type="button" :disabled="busy || !dirty" @click="command('tvSettings', { settings })">{{ busy ? 'Working…' : 'Save TV settings' }}</button>
      <div class="connection" role="status" aria-live="polite">
        <p>{{ settings.enabled ? (status.connected ? 'TV connected' : 'Waiting for TV connection') : 'TV control is off' }}<span v-if="status.connected"> · {{ powerLabel }}</span></p>
        <p v-if="status.currentInput">Current TV input: <strong>{{ inputLabel(status.currentInput) }}</strong></p>
        <p v-if="settings.enabled && !status.paired" class="help">Save your settings, click Pair TV, then accept the connection prompt on the TV with its remote.</p>
        <div class="actions">
          <button type="button" :disabled="busy || dirty || !settings.enabled" @click="command('tvPair')">Pair TV</button>
          <button type="button" :disabled="busy || dirty || !settings.enabled || !status.paired" @click="command('tvSync')">Show current computer</button>
          <button type="button" :disabled="busy || dirty || !settings.enabled || !settings.mac" @click="command('tvWake')">Wake TV</button>
          <button v-if="status.paired" type="button" :disabled="busy || dirty" @click="command('tvForget')">Forget TV pairing</button>
        </div>
      </div>
      <p v-if="error || status.error" class="error" role="alert">{{ error || status.error }}</p>
      <p class="help">Wake follows actual input or an explicit switch. App startup and automatic fallback to the PC do not send wake. TV control runs separately from keyboard and mouse forwarding.</p>
    </template>
  </section>
</template>
<script>
import ApiService from '../api-service'
export default {
  name: 'TvSettings',
  data: () => ({ settings: null, status: {}, busy: false, dirty: false, error: '', interval: null, generation: 0 }),
  computed: {
    inputs () { const found = this.status.inputs || []; return Array.from({ length: 8 }, (_, index) => { const id = 'HDMI_' + (index + 1); const item = found.find(x => x.id === id); return { id, label: 'HDMI ' + (index + 1) + (item ? ' · ' + item.label : '') } }) },
    powerLabel () { return ({ on: 'On', off: 'Standby', 'screen-off': 'Screen off', unknown: 'Power state unknown' })[this.status.power] || 'Power state unknown' }
  },
  mounted () { this.load(); this.interval = setInterval(() => this.load(), 2000) },
  beforeDestroy () { clearInterval(this.interval); this.generation++ },
  methods: {
    changed () { this.dirty = true; this.error = '' },
    inputLabel (id) { const input = this.inputs.find(x => x.id === id); return input ? input.label : id },
    async load () {
      if (this.busy) return
      const generation = this.generation
      try { const data = (await ApiService.getKvm()).tv; if (generation !== this.generation) return; this.status = data.status; if (!this.dirty) this.settings = data.settings; this.error = '' }
      catch (error) { if (generation === this.generation) this.error = error.message || 'Could not reach TV settings. Reconnect to the Pi.' }
    },
    async command (action, values = {}) {
      this.busy = true; this.generation++; this.error = ''
      try { const data = (await ApiService.setKvm({ action, ...values })).tv; this.status = data.status; if (!this.dirty || action === 'tvSettings') this.settings = data.settings; if (action === 'tvSettings') this.dirty = false }
      catch (error) { this.error = error.message || 'TV action failed. Check the TV address and retry.' }
      finally { this.busy = false }
    }
  }
}
</script>
<style scoped>
.tv-settings { padding: 20px; max-width: 620px; color: #333; }
h2 { margin: 0 0 12px; font-size: 24px; font-weight: 500; }
p { line-height: 1.5; }
fieldset { border: 0; margin: 24px 0; padding: 0; }
legend { font-weight: 500; margin-bottom: 12px; }
fieldset > label { display: block; margin-top: 16px; }
.check { display: flex; align-items: center; gap: 8px; margin: 20px 0; }
fieldset > .check { display: flex; }
input[type=checkbox] { width: 18px; height: 18px; accent-color: #1565c0; flex-shrink: 0; }
input[type=text], select { display: block; box-sizing: border-box; width: 100%; min-height: 40px; margin-top: 6px; padding: 8px; border: 1px solid #777; border-radius: 4px; color: #222; background: white; font: inherit; }
.inputs { display: flex; gap: 16px; margin: 24px 0; }
.inputs > label { flex: 1; min-width: 0; }
.help { font-size: 13px; color: #555; }
details { margin: 24px 0; }
summary { cursor: pointer; min-height: 32px; }
button { min-height: 40px; padding: 8px 12px; background: white; border: 1px solid #777; border-radius: 4px; font: inherit; cursor: pointer; color: #222; }
button:hover:not(:disabled) { background: #f1f4f7; }
button:disabled { color: #666; background: #ececec; border-color: #bbb; cursor: default; }
.save { background: #1565c0; color: white; border-color: #1565c0; }
.save:hover:not(:disabled) { background: #0d47a1; }
.connection { margin-top: 24px; padding-top: 12px; border-top: 1px solid #d4d4d4; }
.actions { display: flex; flex-wrap: wrap; gap: 10px; }
.error { color: #a32121; }
button:focus-visible, input:focus-visible, select:focus-visible, summary:focus-visible { outline: 2px solid #1565c0; outline-offset: 3px; }
@media (max-width: 480px) { .inputs { flex-direction: column; } }
</style>
