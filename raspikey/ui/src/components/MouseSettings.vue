<template>
  <section class="mouse-settings" aria-label="Mouse settings">
    <h3>Scroll wheel and buttons</h3>
    <p v-if="loading" role="status">Loading mouse settings…</p>
    <template v-if="settings">
      <label class="field-label" :for="id + '-mode'">Scroll mode</label>
      <select :id="id + '-mode'" v-model="settings.scrollMode" :disabled="saving || disabled" @change="changed">
        <option value="free">Free-spin</option>
        <option value="ratchet">Ratchet — fixed steps</option>
        <option value="smart">SmartShift — release at speed</option>
      </select>
      <p class="field-help">{{ modeHelp }}</p>
      <label v-if="settings.scrollMode === 'smart'" class="field-label" :for="id + '-speed'">SmartShift speed (2–49)
        <input :id="id + '-speed'" v-model.number="settings.smartShift" type="number" min="2" max="49" step="1" :disabled="saving || disabled" @input="changed">
      </label>

      <label class="field-label button-heading" :for="id + '-button'">Customize a button</label>
      <select :id="id + '-button'" v-model="selected">
        <option v-for="button in buttons" :key="button.id" :value="button.id">{{ button.label }}</option>
      </select>
      <label class="field-label" :for="id + '-action'">Button action</label>
      <select :id="id + '-action'" v-model="current.action" :disabled="saving || disabled" @change="changed">
        <option value="default">{{ originalAction }}</option>
        <option value="shortcut">Keyboard shortcut</option>
        <option value="disabled">Do nothing</option>
      </select>
      <template v-if="current.action === 'shortcut'">
        <fieldset :disabled="saving || disabled">
          <legend>Hold these modifiers</legend>
          <label v-for="modifier in modifiers" :key="modifier.value">
            <input type="checkbox" :checked="!!(current.modifiers & modifier.value)" @change="modifierChanged(modifier.value, $event.target.checked)"> {{ modifier.label }}
          </label>
        </fieldset>
        <label class="field-label" :for="id + '-key'">Then send this key</label>
        <select :id="id + '-key'" v-model.number="current.key" :disabled="saving || disabled" @change="changed">
          <option v-for="key in keys" :key="key.value" :value="key.value">{{ key.label }}</option>
        </select>
      </template>
      <p class="field-help" v-if="selected === 'scrollToggle' && current.action !== 'default'">The button above the wheel sends this action. Change the scroll mode here instead.</p>
      <p class="field-help" v-else-if="selected === 'thumb' && current.action === 'default'">The thumb button opens Windows Task View.</p>
      <p class="setting-status" :class="{ 'setting-error': !!error || !!status.error }" role="status" aria-live="polite">{{ statusText }}</p>
      <button class="save-mouse" type="button" :disabled="saving || disabled || !dirty" @click="save">{{ saving ? 'Saving…' : 'Save mouse settings' }}</button>
    </template>
    <p v-else-if="error" class="setting-error" role="alert">{{ error }} <button type="button" @click="load">Retry</button></p>
  </section>
</template>

<script>
import ApiService from '../api-service'
const keys = []
for (let i = 0; i < 26; i++) keys.push({ label: String.fromCharCode(65 + i), value: 4 + i })
for (let i = 1; i <= 9; i++) keys.push({ label: String(i), value: 29 + i })
keys.push({ label: '0', value: 39 })
for (let i = 1; i <= 12; i++) keys.push({ label: 'F' + i, value: 57 + i })
keys.push(...[
  ['Print Screen', 70], ['Tab', 43], ['Enter', 40], ['Escape', 41], ['Space', 44],
  ['Backspace', 42], ['Delete', 76], ['Insert', 73], ['Home', 74], ['End', 77],
  ['Page Up', 75], ['Page Down', 78], ['Right arrow', 79], ['Left arrow', 80], ['Down arrow', 81], ['Up arrow', 82]
].map(([label, value]) => ({ label, value })))
export default {
  name: 'MouseSettings',
  props: { address: { type: String, required: true }, disabled: { type: Boolean, default: false } },
  data: () => ({
    settings: null, status: {}, loading: true, saving: false, dirty: false, error: '', selected: 'scrollToggle', interval: null,
    keys,
    modifiers: [{ label: 'Ctrl', value: 1 }, { label: 'Shift', value: 2 }, { label: 'Alt', value: 4 }, { label: 'Win / Cmd', value: 8 }],
    buttons: [
      { id: 'scrollToggle', label: 'Scroll toggle (above wheel)', original: 'Original scroll-mode toggle' },
      { id: 'back', label: 'Back', original: 'Original Back button' },
      { id: 'forward', label: 'Forward', original: 'Original Forward button' },
      { id: 'thumb', label: 'Thumb / task switcher', original: 'Original Task View shortcut' },
      { id: 'middle', label: 'Middle click', original: 'Original middle click' }
    ]
  }),
  computed: {
    id () { return 'mouse-' + this.address.replace(/:/g, '') },
    current () { return this.settings.buttons[this.selected] },
    originalAction () { return this.buttons.find(b => b.id === this.selected).original },
    modeHelp () {
      return { free: 'The wheel spins freely at every speed.', ratchet: 'The wheel stays in stepped mode.', smart: 'The wheel releases when you scroll quickly. A higher speed requires a faster scroll.' }[this.settings.scrollMode]
    },
    statusText () {
      if (this.error) return this.error
      if (this.dirty) return 'Unsaved changes.'
      if (this.status.error) return this.status.error
      if (!this.status.connected) return 'Saved settings apply when the mouse reconnects.'
      if (!this.status.applied) return 'Applying mouse settings…'
      return 'Settings applied to the mouse.'
    }
  },
  mounted () { this.load(); this.interval = setInterval(() => this.load(), 5000) },
  beforeDestroy () { clearInterval(this.interval) },
  methods: {
    changed () { this.dirty = true; this.error = '' },
    modifierChanged (mask, checked) { this.current.modifiers = checked ? this.current.modifiers | mask : this.current.modifiers & ~mask; this.changed() },
    async load () {
      if (this.saving) return
      try {
        const result = await ApiService.getMouseSettings(this.address)
        this.status = result.status
        if (!this.dirty) this.settings = result.settings
        this.error = ''
      } catch (error) { this.error = error.message || 'Could not reach RaspiKey. Reconnect and retry.' }
      finally { this.loading = false }
    },
    async save () {
      this.saving = true; this.error = ''
      try {
        const result = await ApiService.setMouseSettings(this.address, this.settings)
        this.settings = result.settings; this.status = result.status; this.dirty = false
      } catch (error) { this.error = error.message || 'Could not save mouse settings. Retry.' }
      finally { this.saving = false }
    }
  }
}
</script>

<style scoped>
.mouse-settings { margin-top: 20px; padding-top: 16px; border-top: 1px solid #d4d4d4; color: #333; }
h3 { margin: 0 0 16px; font-size: 18px; font-weight: 500; }
.field-label { display: block; margin: 16px 0 6px; font-weight: 500; }
.button-heading { margin-top: 24px; }
select, input[type=number] { width: 100%; min-height: 40px; padding: 8px; border: 1px solid #777; border-radius: 4px; color: #222; background: #fff; font: inherit; }
.field-help { margin: 8px 0; font-size: 13px; line-height: 1.5; color: #555; }
fieldset { border: 0; padding: 0; margin: 16px 0; display: flex; flex-wrap: wrap; gap: 12px; }
legend { padding: 0; margin-bottom: 8px; font-weight: 500; }
fieldset label { display: inline-flex; align-items: center; gap: 5px; min-height: 32px; }
input[type=checkbox] { width: 18px; height: 18px; accent-color: #1976d2; }
.setting-status { margin: 16px 0 10px; min-height: 20px; font-size: 13px; line-height: 1.5; }
.setting-error { color: #a32121; }
.save-mouse { width: 100%; min-height: 40px; padding: 8px 12px; border: 1px solid #1565c0; border-radius: 4px; background: #1565c0; color: white; font: inherit; cursor: pointer; }
.save-mouse:hover:not(:disabled) { background: #0d47a1; }
.save-mouse:disabled { background: #ececec; color: #666; border-color: #bbb; cursor: default; }
select:focus-visible, input:focus-visible, button:focus-visible { outline: 2px solid #1565c0; outline-offset: 3px; }
</style>
