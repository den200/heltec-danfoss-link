// Heating schedule card: weekly comfort periods for schedule groups and for single
// rooms. A group is a schedule helper named "Heating <group>"; a room's own schedule
// is one named "Room schedule <room>". Each room picks Always comfort, Own schedule
// or a group in input_select.heating_<room>_schedule.
// Loaded as a Lovelace module resource by ha/heating_setup.py.
const DAYS = ['monday', 'tuesday', 'wednesday', 'thursday', 'friday', 'saturday', 'sunday'];
const SHORT = ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun'];
const GROUP = 'Heating ';
const OWN_PREFIX = 'Room schedule ';
const NONE = 'Always comfort';
const OWN = 'Own schedule';

const hm = (t) => (t || '').slice(0, 5);
const minutes = (t) => { const [h, m] = hm(t).split(':').map(Number); return h * 60 + m; };
const endMinutes = (t) => (t.startsWith('24') ? 1440 : minutes(t));
const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
const weekOf = (item) => Object.fromEntries(DAYS.map((d) => [d, (item?.[d] || []).map((p) => ({ from: p.from, to: p.to }))]));

class HeatingScheduleCard extends HTMLElement {
  setConfig(config) {
    this._config = { rooms: [], ...config };
    this._view = null;      // {kind: 'group', id} | {kind: 'room', select}
    this._editing = null;   // day being edited
    this._draft = [];
    this._mode = null;      // 'new' | 'rename' | 'delete'
    this._error = '';
    this._busy = false;
  }

  set hass(hass) {
    const first = !this._hass;
    const before = this._roomState();
    this._hass = hass;
    if (first) this._load();
    else if (!this._editing && !this._mode && !this._busy && before !== this._roomState()) this._render();
  }

  getCardSize() { return 10; }

  _roomState() { return this._hass ? this._config.rooms.map((r) => this._hass.states[r.select]?.state).join('|') : ''; }
  _choice(room) { return this._hass.states[room.select]?.state; }
  get _groups() { return (this._items || []).filter((i) => i.name.startsWith(GROUP)).sort((a, b) => a.name.localeCompare(b.name)); }
  _label(g) { return g.name.slice(GROUP.length); }
  _groupNamed(label) { return this._groups.find((g) => this._label(g) === label); }
  _own(room) { return (this._items || []).find((i) => i.name === OWN_PREFIX + room.name); }
  _room(select) { return this._config.rooms.find((r) => r.select === select); }
  _roomsUsing(label) { return this._config.rooms.filter((r) => this._choice(r) === label).map((r) => r.name); }

  // The schedule shown: a group, or a room's own/followed schedule.
  _shown() {
    const v = this._view;
    if (!v) return { item: null, editable: false };
    if (v.kind === 'group') return { item: this._groups.find((g) => g.id === v.id), editable: true };
    const room = this._room(v.select), choice = this._choice(room);
    if (choice === OWN) return { item: this._own(room), editable: true };
    return { item: this._groupNamed(choice), editable: false };
  }

  async _load() {
    try {
      this._items = await this._hass.callWS({ type: 'schedule/list' });
      this._error = '';
    } catch (e) {
      this._error = `Cannot read schedules: ${e.message || e}`;
      this._items = [];
    }
    const v = this._view;
    const valid = v && (v.kind === 'room' ? this._room(v.select) : this._groups.some((g) => g.id === v.id));
    if (!valid) {
      // Open on a group some room uses, so the first view shows a real week.
      const used = this._groups.find((g) => this._roomsUsing(this._label(g)).length) || this._groups[0];
      this._view = used ? { kind: 'group', id: used.id } : this._config.rooms[0] ? { kind: 'room', select: this._config.rooms[0].select } : null;
    }
    this._render();
  }

  _style() {
    return `<style>
      ha-card { padding: 16px; }
      .title { font-size: 1.25rem; font-weight: 500; margin-bottom: 12px; }
      .label { font-size: 0.75rem; text-transform: uppercase; letter-spacing: .06em; color: var(--secondary-text-color); margin: 4px 0 6px; }
      .chips { display: flex; flex-wrap: wrap; gap: 8px; margin-bottom: 10px; }
      .chip { border: 1px solid var(--divider-color); border-radius: 16px; padding: 6px 14px; cursor: pointer;
              background: none; color: var(--primary-text-color); font: inherit; }
      .chip small { color: var(--secondary-text-color); margin-left: 6px; }
      .chip.on { background: var(--primary-color); border-color: var(--primary-color); color: var(--text-primary-color, #fff); }
      .chip.on small { color: inherit; opacity: .8; }
      .chip.add { border-style: dashed; color: var(--secondary-text-color); }
      .sub { color: var(--secondary-text-color); font-size: 0.9rem; margin: 6px 0 12px; }
      .week { display: grid; grid-template-columns: 40px 1fr; row-gap: 6px; column-gap: 10px; align-items: center; }
      .day { color: var(--secondary-text-color); font-size: 0.85rem; }
      .bar { position: relative; height: 26px; border-radius: 6px; overflow: hidden;
             background: color-mix(in srgb, var(--primary-text-color) 7%, transparent); }
      .bar.edit { cursor: pointer; }
      .bar.edit:hover, .bar.sel { outline: 2px solid var(--primary-color); }
      .week.ro .blk { opacity: .45; }
      .blk { position: absolute; top: 0; bottom: 0; background: var(--state-climate-heat-color, #ff8100); opacity: .85;
             display: flex; align-items: center; justify-content: center; font-size: 0.72rem; color: #fff; white-space: nowrap; }
      .ticks { display: grid; grid-template-columns: 40px 1fr; column-gap: 10px; margin-top: 2px; }
      .scale { position: relative; height: 14px; font-size: 0.7rem; color: var(--secondary-text-color); }
      .scale span { position: absolute; transform: translateX(-50%); }
      .legend { display: flex; gap: 16px; font-size: 0.8rem; color: var(--secondary-text-color); margin-top: 8px; }
      .sw { display: inline-block; width: 10px; height: 10px; border-radius: 2px; margin-right: 4px; vertical-align: -1px; }
      .panel { margin-top: 14px; padding: 12px; border-radius: 8px; border: 1px solid var(--divider-color); }
      .panel h3 { margin: 0 0 8px; font-size: 1rem; font-weight: 500; }
      .row { display: flex; align-items: center; gap: 8px; margin: 6px 0; flex-wrap: wrap; }
      input[type=time], input[type=text], select {
        font: inherit; padding: 6px 8px; border-radius: 6px; border: 1px solid var(--divider-color);
        background: var(--card-background-color); color: var(--primary-text-color); }
      button.btn { font: inherit; padding: 6px 12px; border-radius: 6px; cursor: pointer;
                   border: 1px solid var(--divider-color); background: none; color: var(--primary-text-color); }
      button.primary { background: var(--primary-color); border-color: var(--primary-color); color: var(--text-primary-color, #fff); }
      button.danger { color: var(--error-color); border-color: var(--error-color); }
      button.link { border: none; background: none; color: var(--primary-color); cursor: pointer; font: inherit; padding: 2px 4px; }
      .x { border: none; background: none; cursor: pointer; color: var(--secondary-text-color); font-size: 1.1rem; }
      .err { color: var(--error-color); margin: 8px 0; }
      .muted { color: var(--secondary-text-color); }
    </style>`;
  }

  _render() {
    if (!this._config || !this._hass || !this._items) return;
    const v = this._view || {};
    const groupChips = this._groups.map((g) =>
      `<button class="chip ${v.kind === 'group' && v.id === g.id ? 'on' : ''}" data-group="${esc(g.id)}">${esc(this._label(g))}</button>`).join('');
    const roomChips = this._config.rooms.map((r) => {
      const c = this._choice(r);
      const hint = c === OWN ? 'own' : c === NONE ? 'comfort' : c || '';
      return `<button class="chip ${v.kind === 'room' && v.select === r.select ? 'on' : ''}" data-room="${esc(r.select)}">${esc(r.name)}<small>${esc(hint)}</small></button>`;
    }).join('');
    this.innerHTML = `${this._style()}<ha-card>
      <div class="title">${esc(this._config.title || 'Heating schedules')}</div>
      <div class="label">Rooms</div><div class="chips">${roomChips}</div>
      <div class="label">Groups</div><div class="chips">${groupChips}<button class="chip add" data-act="new">+ New group</button></div>
      ${this._error ? `<div class="err">${esc(this._error)}</div>` : ''}
      ${v.kind === 'room' ? this._roomBody() : this._groupBody()}
      ${this._panel()}</ha-card>`;
    this._bind();
  }

  _groupBody() {
    const { item } = this._shown();
    if (!item) return '<p class="sub">No schedule groups yet. Create one, or give a room its own schedule.</p>';
    const used = this._roomsUsing(this._label(item));
    return `<p class="sub">${used.length ? 'Used by ' + esc(used.join(', ')) : 'Not used by any room yet'}
        · tap a day to edit · <button class="link" data-act="rename">Rename</button>
        <button class="link" data-act="delete">Delete</button></p>${this._week(item, true)}`;
  }

  _roomBody() {
    const room = this._room(this._view.select), choice = this._choice(room);
    const options = [NONE, OWN, ...this._groups.map((g) => this._label(g))];
    let html = `<div class="row"><span>${esc(room.name)} follows</span>
      <select id="follow">${options.map((o) => `<option ${o === choice ? 'selected' : ''}>${esc(o)}</option>`).join('')}</select></div>`;
    if (choice === OWN) {
      const own = this._own(room);
      html += own ? `<p class="sub">Its own schedule · tap a day to edit</p>${this._week(own, true)}`
                  : `<p class="sub">No own schedule yet.</p><button class="btn primary" data-act="make-own">Create its schedule</button>`;
    } else if (choice === NONE) {
      html += `<p class="sub">Holds its comfort temperature all the time.</p>
        <button class="btn" data-act="make-own">${this._own(room) ? 'Switch back to its own schedule' : `Give ${esc(room.name)} its own schedule`}</button>`;
    } else {
      const g = this._groupNamed(choice);
      const own = this._own(room) ? 'Switch back to its own schedule' : 'Give it its own schedule (starts as a copy)';
      html += `<p class="sub">Follows the group “${esc(choice)}” ·
        <button class="link" data-act="open-group">Edit group</button> ·
        <button class="link" data-act="make-own">${own}</button></p>${this._week(g, false)}`;
    }
    return html;
  }

  _week(item, editable) {
    if (!item) return '';
    const rows = DAYS.map((d, i) =>
      `<div class="day">${SHORT[i]}</div><div class="bar ${editable ? 'edit' : ''} ${this._editing === d ? 'sel' : ''}" ${editable ? `data-day="${d}"` : ''}>${this._blocks(item[d] || [])}</div>`).join('');
    return `<div class="week ${editable ? '' : 'ro'}">${rows}</div>
      <div class="ticks"><div></div><div class="scale">${[0, 6, 12, 18, 24].map((h) =>
        `<span style="left:${h / 24 * 100}%">${String(h).padStart(2, '0')}</span>`).join('')}</div></div>
      <div class="legend"><span><span class="sw" style="background:var(--state-climate-heat-color,#ff8100)"></span>Comfort</span>
        <span><span class="sw" style="background:color-mix(in srgb,var(--primary-text-color) 15%,transparent)"></span>Eco</span></div>`;
  }

  _blocks(periods) {
    return periods.map((p) => {
      const a = minutes(p.from), w = (endMinutes(p.to) - a) / 1440 * 100;
      return `<div class="blk" style="left:${a / 1440 * 100}%;width:${w}%">${w > 18 ? `${hm(p.from)}–${hm(p.to)}` : ''}</div>`;
    }).join('');
  }

  _panel() {
    const { item } = this._shown();
    if (this._mode === 'new' || this._mode === 'rename') {
      const value = this._mode === 'rename' && item ? this._label(item) : '';
      return `<div class="panel"><h3>${this._mode === 'new' ? 'New schedule group' : 'Rename group'}</h3>
        <div class="row"><input type="text" id="name" placeholder="e.g. Bedrooms" value="${esc(value)}" maxlength="40">
        <button class="btn primary" data-act="${this._mode}-save">${this._mode === 'new' ? 'Create' : 'Rename'}</button>
        <button class="btn" data-act="cancel">Cancel</button></div>
        ${this._mode === 'new' ? '<p class="sub">Starts with no comfort periods (eco all week); add periods per day.</p>' : ''}</div>`;
    }
    if (this._mode === 'delete' && item) {
      const used = this._roomsUsing(this._label(item));
      return `<div class="panel"><h3>Delete “${esc(this._label(item))}”?</h3>
        <p class="sub">${used.length ? esc(used.join(', ')) + ' will switch to ' + NONE + '.' : 'No room uses it.'}</p>
        <div class="row"><button class="btn danger" data-act="delete-save">Delete group</button>
        <button class="btn" data-act="cancel">Cancel</button></div></div>`;
    }
    if (!this._editing || !item) return '';
    const i = DAYS.indexOf(this._editing);
    const rows = this._draft.map((p, n) => `<div class="row">
        <input type="time" data-n="${n}" data-k="from" value="${hm(p.from)}"> to
        <input type="time" data-n="${n}" data-k="to" value="${p.to.startsWith('24') ? '23:59' : hm(p.to)}">
        <button class="x" data-del="${n}" title="Remove">✕</button></div>`).join('');
    return `<div class="panel"><h3>${SHORT[i]} · comfort periods</h3>
      ${rows || '<p class="sub">No comfort period: eco all day.</p>'}
      <div class="row"><button class="link" data-act="add">+ Add period</button></div>
      <div class="row muted">Also apply to:
        <label><input type="checkbox" data-copy="weekdays"> Mon–Fri</label>
        <label><input type="checkbox" data-copy="weekend"> Sat–Sun</label>
        <label><input type="checkbox" data-copy="all"> every day</label></div>
      <div class="row"><button class="btn primary" data-act="save">Save</button>
        <button class="btn" data-act="cancel">Cancel</button></div>
      <p class="sub">23:59 as an end time means until midnight.</p></div>`;
  }

  _bind() {
    const reset = () => { this._editing = null; this._mode = null; this._error = ''; };
    this.querySelectorAll('[data-group]').forEach((b) => b.addEventListener('click', () => {
      this._view = { kind: 'group', id: b.dataset.group }; reset(); this._render();
    }));
    this.querySelectorAll('[data-room]').forEach((b) => b.addEventListener('click', () => {
      this._view = { kind: 'room', select: b.dataset.room }; reset(); this._render();
    }));
    this.querySelector('#follow')?.addEventListener('change', (e) => this._follow(e.target.value));
    this.querySelectorAll('.bar[data-day]').forEach((b) => b.addEventListener('click', () => {
      this._editing = b.dataset.day; this._mode = null; this._error = '';
      // Save into the schedule shown now, even if the room's choice changes meanwhile.
      this._editingItem = this._shown().item;
      this._draft = (this._editingItem[this._editing] || []).map((p) => ({ ...p }));
      this._render();
    }));
    this.querySelectorAll('input[type=time]').forEach((inp) => inp.addEventListener('change', () => {
      const p = this._draft[Number(inp.dataset.n)];
      p[inp.dataset.k] = inp.dataset.k === 'to' && inp.value === '23:59' ? '24:00:00' : `${inp.value}:00`;
    }));
    this.querySelectorAll('[data-del]').forEach((b) => b.addEventListener('click', () => {
      this._draft.splice(Number(b.dataset.del), 1); this._render();
    }));
    this.querySelectorAll('[data-act]').forEach((b) => b.addEventListener('click', () => this._act(b.dataset.act)));
  }

  async _act(act) {
    if (this._busy) return; // a double tap must not create two groups
    if (act === 'new' || act === 'rename' || act === 'delete') { this._mode = act; this._editing = null; this._render(); return; }
    if (act === 'cancel') { this._mode = null; this._editing = null; this._error = ''; this._render(); return; }
    if (act === 'open-group') {
      const g = this._groupNamed(this._choice(this._room(this._view.select)));
      if (g) { this._view = { kind: 'group', id: g.id }; this._render(); }
      return;
    }
    if (act === 'add') {
      const last = this._draft[this._draft.length - 1];
      this._draft.push(last && !last.to.startsWith('24') ? { from: last.to, to: '24:00:00' } : { from: '07:00:00', to: '22:00:00' });
      this._render(); return;
    }
    await this._run(async () => {
      if (act === 'save') await this._saveDay();
      if (act === 'new-save') await this._create();
      if (act === 'rename-save') await this._rename();
      if (act === 'delete-save') await this._delete();
      if (act === 'make-own') await this._makeOwn();
    });
  }

  async _run(fn) {
    this._busy = true;
    try {
      await fn();
      this._mode = null; this._editing = null; this._error = '';
      await this._load();
    } catch (e) {
      this._error = e.message || String(e);
      this._render();
    } finally { this._busy = false; }
  }

  // Re-read the schedule first: the whole week is sent, and it may have been edited
  // elsewhere since this card loaded it.
  async _update(item, changes) {
    const fresh = (await this._hass.callWS({ type: 'schedule/list' })).find((i) => i.id === item.id);
    if (!fresh) throw new Error('That schedule no longer exists');
    const body = { type: 'schedule/update', schedule_id: fresh.id, name: fresh.name, ...weekOf(fresh) };
    if (fresh.icon) body.icon = fresh.icon;
    return this._hass.callWS({ ...body, ...changes });
  }

  // The controller automation triggers on each group/own schedule by entity ID;
  // keep that list in step when schedules are created or deleted here.
  async _syncController() {
    const id = 'heating_controller';
    const cfg = await this._hass.callApi('GET', `config/automation/config/${id}`);
    const items = new Set((await this._hass.callWS({ type: 'schedule/list' }))
      .filter((i) => i.name.startsWith(GROUP) || i.name.startsWith(OWN_PREFIX)).map((i) => i.id));
    const entities = (await this._hass.callWS({ type: 'config/entity_registry/list' }))
      .filter((e) => e.entity_id.startsWith('schedule.') && items.has(e.unique_id)).map((e) => e.entity_id).sort();
    const triggers = cfg.triggers || cfg.trigger || [];
    let trig = triggers.find((t) => t.id === 'schedule');
    if (!entities.length) {
      // The last schedule was deleted: drop its trigger too (an empty list is invalid).
      if (!trig) return;
      triggers.splice(triggers.indexOf(trig), 1);
      return this._hass.callApi('POST', `config/automation/config/${id}`, cfg);
    }
    if (trig && JSON.stringify(trig.entity_id) === JSON.stringify(entities)) return;
    if (!trig) {
      trig = { trigger: 'state', to: ['on', 'off'], for: { minutes: 1 }, id: 'schedule' };
      triggers.splice(1, 0, trig);
    }
    trig.entity_id = entities;
    await this._hass.callApi('POST', `config/automation/config/${id}`, cfg);
  }

  async _saveDay() {
    const periods = this._draft.map((p) => ({ from: p.from.length === 5 ? p.from + ':00' : p.from, to: p.to.length === 5 ? p.to + ':00' : p.to }))
      .sort((a, b) => minutes(a.from) - minutes(b.from));
    for (const [n, p] of periods.entries()) {
      if (endMinutes(p.to) <= minutes(p.from)) throw new Error(`Period ${hm(p.from)}–${hm(p.to)} ends before it starts`);
      if (n && minutes(p.from) < endMinutes(periods[n - 1].to)) throw new Error('Periods overlap');
    }
    const targets = new Set([this._editing]);
    this.querySelectorAll('[data-copy]').forEach((c) => {
      if (!c.checked) return;
      (c.dataset.copy === 'weekdays' ? DAYS.slice(0, 5) : c.dataset.copy === 'weekend' ? DAYS.slice(5) : DAYS).forEach((d) => targets.add(d));
    });
    const changes = {};
    targets.forEach((d) => { changes[d] = periods; });
    await this._update(this._editingItem, changes);
  }

  // A room's own schedule starts as a copy of what it follows now, then the room switches to it.
  async _makeOwn() {
    const room = this._room(this._view.select);
    if (!this._own(room)) {
      // From a group: a copy of its week. From Always comfort: comfort all day, so the
      // room keeps its temperature until periods are edited.
      // Fresh copy: the group may have been edited elsewhere since this card loaded.
      const label = this._choice(room);
      const followed = (await this._hass.callWS({ type: 'schedule/list' })).find((i) => i.name === GROUP + label);
      const week = followed ? weekOf(followed) : Object.fromEntries(DAYS.map((d) => [d, [{ from: '00:00:00', to: '24:00:00' }]]));
      await this._hass.callWS({ type: 'schedule/create', name: OWN_PREFIX + room.name, icon: 'mdi:calendar-account', ...week });
      await this._syncController();
    }
    await this._select(room, OWN);
  }

  async _follow(choice) {
    if (this._busy) return;
    const room = this._room(this._view.select);
    await this._run(async () => {
      if (choice === OWN && !this._own(room)) { await this._makeOwn(); return; }
      await this._select(room, choice);
    });
  }

  _select(room, option) {
    return this._hass.callService('input_select', 'select_option', { entity_id: room.select, option });
  }

  _name() {
    const name = (this.querySelector('#name')?.value || '').trim();
    if (!name) throw new Error('Give the group a name');
    if (name === NONE || name === OWN) throw new Error(`"${name}" is reserved`);
    const current = this._mode === 'rename' ? this._view.id : null;
    if (this._groups.some((g) => this._label(g).toLowerCase() === name.toLowerCase() && g.id !== current))
      throw new Error('A group with that name exists');
    return name;
  }

  async _setRoomOptions(rename = null) {
    // Every room selector lists the same choices; keep each room's choice (following a rename).
    const items = await this._hass.callWS({ type: 'input_select/list' });
    const groups = (await this._hass.callWS({ type: 'schedule/list' }))
      .filter((i) => i.name.startsWith(GROUP)).map((i) => i.name.slice(GROUP.length)).sort();
    const options = [NONE, OWN, ...groups];
    // While renaming, keep the old name listed until rooms have moved, so no room
    // passes through "Always comfort" (that could cost a head update).
    const interim = rename ? [...options, rename[0]] : options;
    const registry = await this._hass.callWS({ type: 'config/entity_registry/list' });
    for (const r of this._config.rooms) {
      const entry = registry.find((e) => e.entity_id === r.select);
      const item = entry && items.find((i) => i.id === entry.unique_id);
      if (!item) continue;
      let cur = this._choice(r);
      if (rename && cur === rename[0]) cur = rename[1];
      const update = (opts) => this._hass.callWS({ type: 'input_select/update', input_select_id: item.id,
                                                   name: item.name, options: opts, ...(item.icon ? { icon: item.icon } : {}) });
      await update(interim);
      if (cur && options.includes(cur) && this._choice(r) !== cur) await this._select(r, cur);
      if (interim !== options) await update(options);
    }
  }

  async _create() {
    const name = this._name();
    const item = await this._hass.callWS({ type: 'schedule/create', name: GROUP + name, icon: 'mdi:calendar-clock' });
    this._view = { kind: 'group', id: item.id };
    await this._setRoomOptions();
    await this._syncController();
  }

  async _rename() {
    const g = this._shown().item, name = this._name(), old = this._label(g);
    if (name === old) return; // nothing to rename (HA rejects the duplicate interim option)
    await this._update(g, { name: GROUP + name });
    await this._setRoomOptions([old, name]);
  }

  async _delete() {
    const g = this._shown().item, label = this._label(g);
    for (const r of this._config.rooms) if (this._choice(r) === label) await this._select(r, NONE);
    await this._hass.callWS({ type: 'schedule/delete', schedule_id: g.id });
    this._view = null;
    await this._setRoomOptions();
    await this._syncController();
  }
}

customElements.define('heating-schedule-card', HeatingScheduleCard);
window.customCards = window.customCards || [];
window.customCards.push({ type: 'heating-schedule-card', name: 'Heating schedule', description: 'Edit heating schedules for groups and rooms' });
