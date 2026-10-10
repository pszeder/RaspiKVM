<template>
  <section class="host-settings" aria-label="Host switching">
    <h2>Host switching</h2>
    <p>Send the keyboard and mouse to the USB computer or a paired Bluetooth computer.</p>
    <p v-if="!settings" role="status">{{ error || 'Loading host settings…' }}</p>
    <template v-else>
      <div class="active-host" role="status" aria-live="polite">Input goes to <strong>{{ status.activeHost === 'bluetooth' ? (status.hostName || 'Bluetooth computer') : 'USB computer' }}</strong>.</div>
      <div class="host-actions">
        <button type="button" :disabled="busy || status.activeHost === 'usb'" @click="command('switch', { host: 'usb' })">Use USB computer</button>
        <button type="button" :disabled="busy || !status.ready || status.activeHost === 'bluetooth'" @click="command('switch', { host: 'bluetooth' })">Use Bluetooth computer</button>
      </div>
      <label class="enable"><input type="checkbox" v-model="settings.enabled" :disabled="busy" @change="changed"> Enable Bluetooth output</label>
      <fieldset :disabled="busy">
        <legend>Switch shortcut</legend>
        <div class="modifiers">
          <label v-for="modifier in modifiers" :key="modifier.value"><input type="checkbox" :checked="!!(settings.shortcut.modifiers & modifier.value)" @change="modifierChanged(modifier.value, $event.target.checked)"> {{ modifier.label }}</label>
        </div>
        <label for="host-switch-key">Key</label>
        <select id="host-switch-key" v-model.number="settings.shortcut.key" @change="changed"><option v-for="key in keys" :key="key.value" :value="key.value">{{ key.label }}</option></select>
      </fieldset>
      <p class="help">Use Ctrl, Alt or Win/Cmd with a key. The shortcut switches both devices and is not sent to either computer. Release held keys and mouse buttons before continuing on the new host.</p>
      <button class="save" type="button" :disabled="busy || !dirty || !(settings.shortcut.modifiers & 13)" @click="save">{{ busy ? 'Working…' : 'Save host settings' }}</button>
      <div class="pairing">
        <h3>Bluetooth computer</h3>
        <p v-if="settings.hostAddress">{{ status.hostName || 'Paired computer' }} · {{ status.ready ? 'Ready to receive input' : 'Waiting for Bluetooth connection' }}</p>
        <p v-else>Save with Bluetooth output enabled, then open pairing. On the Mac, open System Settings → Bluetooth and connect to <strong>RaspiKey KVM</strong>.</p>
        <p v-if="status.pairing" role="status">Pairing is open for {{ status.pairingSeconds }} seconds. Keep the keyboard and mouse connected to RaspiKey.</p>
        <div class="host-actions">
          <button type="button" :disabled="busy || dirty || !settings.enabled || !status.registered || !!settings.hostAddress || status.pairing" @click="command('pair')">Open pairing for 2 minutes</button>
          <button v-if="settings.hostAddress" type="button" :disabled="busy" @click="command('forget')">Forget output host</button>
        </div>
      </div>
      <p v-if="error || status.error" class="error" role="alert">{{ error || status.error }}</p>
      <p class="help">Bluetooth disconnects return input to USB. RaspiKey starts on USB after a restart. USB networking stays available on the PC whichever host receives input.</p>
    </template>
  </section>
</template>
<script>
import ApiService from '../api-service'
const keys = []
for (let i = 1; i <= 12; i++) keys.push({ label: 'F' + i, value: 57 + i })
for (let i = 0; i < 26; i++) keys.push({ label: String.fromCharCode(65 + i), value: 4 + i })
keys.push({ label: 'Space', value: 44 }, { label: 'Tab', value: 43 })
export default {
  name: 'KvmSettings',
  data: () => ({ settings: null, status: {}, dirty: false, busy: false, error: '', interval: null, generation: 0, keys,
    modifiers: [{ label: 'Ctrl', value: 1 }, { label: 'Alt', value: 4 }, { label: 'Shift', value: 2 }, { label: 'Win / Cmd', value: 8 }] }),
  mounted () { this.load(); this.interval = setInterval(() => this.load(), 2000) },
  beforeDestroy () { clearInterval(this.interval); this.generation++ },
  methods: {
    changed () { this.dirty = true; this.error = '' },
    modifierChanged (mask, checked) { this.settings.shortcut.modifiers = checked ? this.settings.shortcut.modifiers | mask : this.settings.shortcut.modifiers & ~mask; this.changed() },
    async load () {
      if (this.busy) return
      const generation = this.generation
      try { const data = await ApiService.getKvm(); if (generation !== this.generation) return; this.status = data.status; if (!this.dirty) this.settings = data.settings; this.error = '' }
      catch (error) { if (generation === this.generation) this.error = error.message || 'Could not reach RaspiKey. Reconnect to the USB network.' }
    },
    async command (action, values = {}) {
      this.busy = true; this.generation++; this.error = ''
      try { const data = await ApiService.setKvm({ action, ...values }); this.status = data.status; if (action === 'settings' || !this.dirty) this.settings = data.settings; if (action === 'settings') this.dirty = false }
      catch (error) { this.error = error.message || 'Host action failed. Retry.' }
      finally { this.busy = false }
    },
    save () { return this.command('settings', { settings: this.settings }) }
  }
}
</script>
<style scoped>
.host-settings { padding: 20px; max-width: 620px; color: #333; }
h2 { margin: 0 0 12px; font-size: 24px; font-weight: 500; }
h3 { margin: 0 0 12px; font-size: 18px; font-weight: 500; }
p { line-height: 1.5; }
.active-host { margin: 20px 0 12px; }
.host-actions { display: flex; flex-wrap: wrap; gap: 10px; }
button { min-height: 40px; padding: 8px 12px; background: white; border: 1px solid #777; border-radius: 4px; font: inherit; cursor: pointer; color: #222; }
button:hover:not(:disabled) { background: #f1f4f7; }
button:disabled { color: #666; background: #ececec; border-color: #bbb; cursor: default; }
.enable { display: flex; align-items: center; gap: 8px; margin: 24px 0; }
input[type=checkbox] { width: 18px; height: 18px; accent-color: #1565c0; }
fieldset { border: 0; margin: 0; padding: 0; }
legend { font-weight: 500; margin-bottom: 12px; }
.modifiers { display: flex; flex-wrap: wrap; gap: 16px; margin-bottom: 14px; }
.modifiers label { display: flex; align-items: center; gap: 5px; min-height: 32px; }
select { display: block; width: 100%; min-height: 40px; margin-top: 6px; padding: 8px; border: 1px solid #777; border-radius: 4px; color: #222; background: white; font: inherit; }
.help { font-size: 13px; color: #555; }
.save { background: #1565c0; color: white; border-color: #1565c0; }
.save:hover:not(:disabled) { background: #0d47a1; }
.pairing { margin-top: 24px; padding-top: 20px; border-top: 1px solid #d4d4d4; }
.error { color: #a32121; }
button:focus-visible, input:focus-visible, select:focus-visible { outline: 2px solid #1565c0; outline-offset: 3px; }
</style>
