#!/usr/bin/env python3
"""Create or update the whole-house heating setup in Home Assistant. Re-runnable.

Rooms, helpers, schedules, the controller script/automations and the dashboard
are defined here so the setup can be rebuilt after an HA loss. Each run changes
only what differs. Needs the `websockets` package and ~/.ha_token.

  heating_setup.py            apply
  heating_setup.py --dry-run  show what would be created/updated
"""
import asyncio, base64, json, os, sys, urllib.request
from pathlib import Path
import websockets

TOKEN = open(os.path.expanduser('~/.ha_token')).read().strip()
DRY = '--dry-run' in sys.argv

# Rooms, heads and the HA address live in household.private.json (gitignored; see
# household.example.json). rooms: key -> (name, climate heads, temperature entity,
# default comfort, default eco, default schedule)
HOUSEHOLD = json.loads(Path(__file__).with_name('household.private.json').read_text())
HA = HOUSEHOLD['ha']
ROOMS = {k: (r['name'], r['heads'], r['temperature'], r['comfort'], r['eco'], r['schedule'])
         for k, r in HOUSEHOLD['rooms'].items()}
# Schedule groups are schedule helpers named "Heating <group>", created and edited in
# the dashboard's schedule card. "Always comfort" means no schedule: the room holds
# its comfort temperature. Everything below looks groups up by name at run time.
GROUP_PREFIX = 'Heating '
NO_GROUP = 'Always comfort'
OWN = 'Own schedule'            # the room's own schedule helper, named OWN_PREFIX + room name
OWN_PREFIX = 'Room schedule '
# Earlier versions summed every group into one template sensor; it capped at 255
# characters and merged near-simultaneous switches. Removed on the next run.
OLD_GROUP_STATES = 'sensor.heating_schedule_groups'
CARD = Path(__file__).with_name('cards') / 'heating-schedule-card.js'
# Away covers short absences and holidays alike. There is no summer mode: a
# room warmer than its setpoint simply does not heat.
MODES = ['Schedule', 'At Home', 'Away']
LINK_NODES = {int(n): k for n, k in HOUSEHOLD['link_nodes'].items()}
# Spare heads (not on a radiator): diagnostics only.
TEST_NODES = {int(n): name for n, name in HOUSEHOLD.get('test_nodes', {}).items()}
# Earlier versions of this setup; removed on the next run.
REMOVED_HELPERS = [('input_number', 'input_number.heating_a_comfort'), ('input_number', 'input_number.heating_a_eco'),
                   ('input_select', 'input_select.heating_a_schedule')]
REMOVED_AUTOMATIONS = ['heating_living_room_sync']
# Replaced by the controller below; disabled, not deleted, for rollback.
OLD_AUTOMATIONS = ['automation.heating_at_home_mode', 'automation.heating_holiday_mode',
                   'automation.heating_office_schedule_comfort', 'automation.heating_office_schedule_eco',
                   'automation.heating_apply_schedule_on_mode_change']


def rest(method, path, body=None, quiet=False):
    req = urllib.request.Request(f'http://{HA}{path}', method=method,
                                 data=json.dumps(body).encode() if body is not None else None,
                                 headers={'Authorization': f'Bearer {TOKEN}', 'Content-Type': 'application/json'})
    try:
        with urllib.request.urlopen(req) as r:
            return json.loads(r.read() or b'null')
    except urllib.error.HTTPError as e:
        if quiet and e.code in (400, 404):
            raise LookupError(path)
        sys.exit(f'{method} {path}: {e.code} {e.read().decode()[:400]}')


class Ws:
    async def __aenter__(self):
        self.ws = await websockets.connect(f'ws://{HA}/api/websocket', max_size=None)
        await self.ws.recv()
        await self.ws.send(json.dumps({'type': 'auth', 'access_token': TOKEN}))
        assert json.loads(await self.ws.recv())['type'] == 'auth_ok'
        self.n = 0
        return self

    async def __aexit__(self, *a):
        await self.ws.close()

    async def __call__(self, **msg):
        self.n += 1
        await self.ws.send(json.dumps(dict(msg, id=self.n)))
        while True:
            r = json.loads(await self.ws.recv())
            if r.get('id') == self.n and r.get('type') == 'result':
                if not r['success']:
                    raise RuntimeError(f"{msg['type']}: {r['error']}")
                return r['result']


def say(*a):
    print(('DRY ' if DRY else '') + ' '.join(map(str, a)))


# ---------- helpers ----------

def helper_specs(group_options=(NO_GROUP,)):
    """(domain, entity_id, create-fields, value to set only when newly created)."""
    specs = [
        ('input_number', 'input_number.heating_holiday_temp',
         {'name': 'Heating Holiday Temp', 'min': 5, 'max': 18, 'step': 0.5, 'mode': 'slider',
          'unit_of_measurement': '°C', 'icon': 'mdi:airplane'}, 14),
        ('input_select', 'input_select.heating_mode', {'name': 'Heating mode', 'options': MODES, 'icon': 'mdi:home-thermometer'}, None),
        ('input_datetime', 'input_datetime.heating_back_home',
         {'name': 'Heating back home', 'has_date': True, 'has_time': True, 'icon': 'mdi:home-import-outline'}, None),
    ]
    for key, (name, _, _, comfort, eco, group) in ROOMS.items():
        for kind, value, icon in (('comfort', comfort, 'mdi:sun-thermometer'), ('eco', eco, 'mdi:snowflake-thermometer')):
            specs.append(('input_number', f'input_number.heating_{key}_{kind}',
                          {'name': f'Heating {name} {kind}', 'min': 5, 'max': 25, 'step': 0.5, 'mode': 'box',
                           'unit_of_measurement': '°C', 'icon': icon}, value))
        specs.append(('input_select', f'input_select.heating_{key}_schedule',
                      {'name': f'Heating {name} schedule', 'options': list(group_options), 'icon': 'mdi:calendar-clock'}, group))
    return specs


async def group_options(ws):
    names = sorted(i['name'][len(GROUP_PREFIX):] for i in await ws(type='schedule/list') if i['name'].startswith(GROUP_PREFIX))
    return [NO_GROUP, OWN] + names


async def ensure_helpers(ws, states):
    created = []
    for domain, entity_id, fields, initial in helper_specs(await group_options(ws)):
        items = await ws(type=f'{domain}/list')
        registry = await ws(type='config/entity_registry/get', entity_id=entity_id) if entity_id in states else None
        if registry:
            item = next((i for i in items if i['id'] == registry['unique_id']), None)
            # Room selectors follow the existing groups; their order is not ours to fix.
            if domain == 'input_select' and item and set(item['options']) != set(fields['options']):
                say('update options', entity_id)
                if not DRY:
                    # Keep the user's name and icon; only the option list is ours.
                    await ws(type='input_select/update', input_select_id=item['id'], name=item['name'],
                             options=fields['options'], **({'icon': item['icon']} if item.get('icon') else {}))
            continue
        say('create', entity_id)
        if DRY:
            continue
        item = await ws(type=f'{domain}/create', **fields)
        created.append((domain, entity_id, initial))
    return created


async def set_initial(created):
    for domain, entity_id, initial in created:
        if initial is None:
            continue
        if domain == 'input_number':
            rest('POST', '/api/services/input_number/set_value', {'entity_id': entity_id, 'value': initial})
        elif domain == 'input_select':
            # A fresh HA has no groups yet: a room meant for "Office" starts on Always comfort.
            options = rest('GET', f'/api/states/{entity_id}')['attributes'].get('options', [])
            if initial in options:
                rest('POST', '/api/services/input_select/select_option', {'entity_id': entity_id, 'option': initial})
            else:
                say(f'{entity_id}: group {initial!r} missing, left on {NO_GROUP}')


# HA derives new MQTT entity IDs from area + device + name (sensor.bedroom_bedroom_...).
HEAD_ENTITY_NAMES = {'last_seen': 'last_report', 'rssi': 'signal_strength', 'updates_left': 'setpoint_updates_left',
                     'valve_opening': 'valve_opening', 'valve_temperature': 'valve_temperature',
                     'battery_level': 'battery_level'}


async def normalise_head_entity_ids(ws):
    for entry in await ws(type='config/entity_registry/list'):
        uid = entry.get('unique_id') or ''
        if entry.get('platform') != 'mqtt' or not uid.startswith('danfoss_ffd2bb2d_'):
            continue
        node, _, kind = uid[len('danfoss_ffd2bb2d_'):].partition('_')
        if not node.isdigit() or kind not in HEAD_ENTITY_NAMES:
            continue
        wanted = f'sensor.danfoss_heltec_{HEAD_ENTITY_NAMES[kind]}_node_{node}'
        if entry['entity_id'] != wanted:
            say('rename', entry['entity_id'], '->', wanted)
            if not DRY:
                await ws(type='config/entity_registry/update', entity_id=entry['entity_id'], new_entity_id=wanted)


async def schedule_entities(ws):
    """Entity IDs of every group and own-room schedule; the controller triggers on each."""
    items = {i['id'] for i in await ws(type='schedule/list')
             if i['name'].startswith(GROUP_PREFIX) or i['name'].startswith(OWN_PREFIX)}
    return sorted(e['entity_id'] for e in await ws(type='config/entity_registry/list')
                  if e['entity_id'].startswith('schedule.') and e.get('unique_id') in items)


async def remove_old_group_states(ws, states):
    if OLD_GROUP_STATES not in states:
        return
    entry = (await ws(type='config/entity_registry/get', entity_id=OLD_GROUP_STATES))['config_entry_id']
    say('delete template sensor', OLD_GROUP_STATES)
    if not DRY:
        rest('DELETE', f'/api/config/config_entries/entry/{entry}')


async def ensure_card_resource(ws):
    url = 'data:text/javascript;base64,' + base64.b64encode(CARD.read_bytes()).decode()
    resources = await ws(type='lovelace/resources')
    mine = [r for r in resources if r['url'].startswith('data:text/javascript') and 'heating-schedule-card' in
            base64.b64decode(r['url'].split(',', 1)[1] + '==').decode(errors='ignore')]
    if mine and mine[0]['url'] == url:
        return
    say('update' if mine else 'create', 'dashboard resource heating-schedule-card')
    if DRY:
        return
    if mine:
        await ws(type='lovelace/resources/update', resource_id=mine[0]['id'], res_type='module', url=url)
    else:
        await ws(type='lovelace/resources/create', res_type='module', url=url)


async def ensure_living_room_mean(ws, states):
    entity_id = 'sensor.heating_living_room_temperature'
    if entity_id in states:
        return
    say('create group sensor', entity_id)
    if DRY:
        return
    flow = rest('POST', '/api/config/config_entries/flow', {'handler': 'group', 'show_advanced_options': False})
    flow = rest('POST', f"/api/config/config_entries/flow/{flow['flow_id']}", {'next_step_id': 'sensor'})
    rest('POST', f"/api/config/config_entries/flow/{flow['flow_id']}", {
        'name': 'Heating Living room temperature', 'type': 'mean', 'hide_members': False,
        'entities': ['sensor.danfoss_heltec_room_temperature_estimate_node_2',
                     'sensor.danfoss_heltec_room_temperature_estimate_node_6']})


# ---------- controller ----------

ROOM_MAP = {k: {'name': v[0], 'heads': v[1]} for k, v in ROOMS.items()}

TARGET = """{% set mode = states('input_select.heating_mode') %}
{% set group = states('input_select.heating_' ~ key ~ '_schedule') %}
{% set sname = ('Room schedule ' ~ rooms[key].name) if group == 'Own schedule' else ('Heating ' ~ group) %}
{% set sched = states.schedule | selectattr('name', 'eq', sname) | map(attribute='entity_id') | first | default('') %}
{% set comfort = states('input_number.heating_' ~ key ~ '_comfort') | float(0) %}
{% set eco = states('input_number.heating_' ~ key ~ '_eco') | float(0) %}
{% if mode == 'At Home' %}{{ comfort }}
{% elif mode == 'Away' %}{{ states('input_number.heating_holiday_temp') | float(0) }}
{% elif sched and is_state(sched, 'off') %}{{ eco }}
{% else %}{{ comfort }}{% endif %}"""

# Rooms affected by a changed entity: the rooms following a switched schedule (group
# or own), or the room of a changed helper/head. No entity (mode change): every room.
AFFECTED = """{% set e = entity | default('') %}
{% set sname = state_attr(e, 'friendly_name') if e.startswith('schedule.') else '' %}
{% set ns = namespace(keys=[]) %}
{% for key, room in rooms.items() %}
  {% set helper = e.startswith('input_number.heating_' ~ key ~ '_') or e == 'input_select.heating_' ~ key ~ '_schedule' %}
  {% set choice = states('input_select.heating_' ~ key ~ '_schedule') %}
  {% set scheduled = sname and (sname == 'Heating ' ~ choice or (choice == 'Own schedule' and sname == 'Room schedule ' ~ room.name)) %}
  {% if helper or scheduled or e in room.heads %}{% set ns.keys = ns.keys + [key] %}{% endif %}
{% endfor %}
{{ ns.keys if e else rooms.keys() | list }}"""

SCRIPT = {
    'alias': 'Heating: apply room targets',
    'description': 'Sets each affected room to comfort, eco or away temperature from the mode and its schedule group. '
                   'Heads already at the target are left alone (no Link update used).',
    'icon': 'mdi:radiator',
    'mode': 'queued',
    'fields': {'entity': {'description': 'Entity whose change triggered this', 'selector': {'text': {}}},
},
    'variables': {'rooms': ROOM_MAP},
    'sequence': [
        {'variables': {'selected': AFFECTED}},
        {'repeat': {'for_each': '{{ selected }}', 'sequence': [
            {'variables': {'key': '{{ repeat.item }}', 'target': '{% set key = repeat.item %}' + TARGET}},
            {'condition': 'template', 'value_template': '{{ target | float(0) >= 5 }}'},
            {'repeat': {'for_each': '{{ rooms[key].heads }}', 'sequence': [
                # Link heads report requested_target while a change is pending; compare
                # with that, or a change still queued for a sleeping head would be missed.
                {'if': [{'condition': 'template', 'value_template':
                         "{% set cur = state_attr(repeat.item, 'requested_target') %}"
                         "{% if cur is none %}{% set cur = state_attr(repeat.item, 'temperature') %}{% endif %}"
                         "{{ states(repeat.item) not in ['unavailable', 'unknown'] and cur | float(-1) != target | float }}"}],
                 'then': [{'action': 'climate.set_temperature', 'target': {'entity_id': '{{ repeat.item }}'},
                           'data': {'temperature': '{{ target | float }}'}}]}]}}]}},
    ],
}

ROOM_HELPERS = [f'input_number.heating_{k}_{kind}' for k in ROOMS for kind in ('comfort', 'eco')] + \
               [f'input_select.heating_{k}_schedule' for k in ROOMS]
ALL_HEADS = [h for r in ROOMS.values() for h in r[1]]

BRIDGE = 'sensor.danfoss_heltec_checkpoint_generation'
PHONE = 'notify.mobile_app_iphone15'  # person.den's phone


def notify(title, message):
    return [{'action': PHONE, 'data': {'title': title, 'message': message}},
            {'action': 'persistent_notification.create', 'data': {'title': title, 'message': message}}]


# A head back from unavailable only catches up when one of its room's inputs changed
# while it was away (a command sent then was refused). Otherwise a bridge restart
# would undo a knob change on the head and spend a setpoint update on it.
MISSED = """{% set since = trigger.from_state.last_changed %}
{% set ns = namespace(missed=false) %}
{% for key, room in rooms.items() if trigger.entity_id in room.heads %}
  {% set group = states('input_select.heating_' ~ key ~ '_schedule') %}
  {% set sname = ('Room schedule ' ~ room.name) if group == 'Own schedule' else ('Heating ' ~ group) %}
  {% set inputs = ['input_select.heating_mode', 'input_number.heating_holiday_temp',
                   'input_number.heating_' ~ key ~ '_comfort', 'input_number.heating_' ~ key ~ '_eco',
                   'input_select.heating_' ~ key ~ '_schedule']
                  + (states.schedule | selectattr('name', 'eq', sname) | map(attribute='entity_id') | list) %}
  {% for e in inputs if states[e] is not none and states[e].last_changed > since %}{% set ns.missed = true %}{% endfor %}
{% endfor %}
{{ ns.missed }}"""


def controller(schedules):
    return {
        'alias': 'Heating: controller',
        'description': 'Applies room targets when the mode, the away temperature, a schedule group or a room setting changes, '
                       'and to a head that missed such a change while unavailable. '
                       'A manual change on a thermostat holds until the next such change.',
        'mode': 'queued',
        'variables': {'rooms': ROOM_MAP},
        'triggers': [
            # to: null ignores attribute-only changes (e.g. a selector's option list).
            # The away temperature applies to every room (only those in Away change).
            {'trigger': 'state', 'entity_id': ['input_select.heating_mode', 'input_number.heating_holiday_temp'],
             'to': None, 'id': 'mode'},
            # Held a minute: a block ending at midnight next to one starting then must
            # not flip the heads. The schedule card keeps this list current.
            *([{'trigger': 'state', 'entity_id': schedules, 'to': ['on', 'off'], 'for': {'minutes': 1}, 'id': 'schedule'}]
              if schedules else []),
            {'trigger': 'state', 'entity_id': ROOM_HELPERS, 'to': None, 'id': 'room'},
            {'trigger': 'state', 'entity_id': ALL_HEADS, 'from': 'unavailable', 'id': 'head'},
            {'trigger': 'time', 'at': 'input_datetime.heating_back_home', 'id': 'back_home'},
        ],
        'conditions': [],
        'actions': [{'choose': [
            {'conditions': [{'condition': 'trigger', 'id': 'back_home'}],
             'sequence': [{'condition': 'state', 'entity_id': 'input_select.heating_mode', 'state': 'Away'},
                          {'action': 'input_select.select_option', 'target': {'entity_id': 'input_select.heating_mode'},
                           'data': {'option': 'Schedule'}}]},
            {'conditions': [{'condition': 'trigger', 'id': 'mode'}],
             'sequence': [{'action': 'script.heating_apply', 'data': {'entity': ''}}]},
            {'conditions': [{'condition': 'trigger', 'id': 'head'}],
             'sequence': [{'condition': 'template', 'value_template': MISSED},
                          {'action': 'script.heating_apply', 'data': {'entity': '{{ trigger.entity_id }}'}}]}],
            'default': [{'action': 'script.heating_apply', 'data': {'entity': '{{ trigger.entity_id }}'}}]}],
    }


# Radiator check. In the first days of data a warm radiator's valve-side sensor
# read 4-6 °C above the room within an hour of opening (living room node 2,
# Master bedroom); node 6 reached ~1.5 °C, node 8 (valve at 100 %) 0.3 °C.
WARM_OPEN, WARM_RISE = 30, 2.5  # valve % held for an hour; valve side minus room, °C
RADIATOR_VARS = {
    'n': "{{ trigger.entity_id.split('_')[-1] }}",
    'vt': "{{ states('sensor.danfoss_heltec_valve_temperature_node_' ~ n) | float(none) }}",
    'room': "{{ states('sensor.danfoss_heltec_room_temperature_estimate_node_' ~ n) | float(none) }}",
}
RADIATOR_COLD = "{{ vt | is_number and room | is_number and vt - room < warm }}"
OTHERS_WARM = """{% set ns = namespace(warm=false) %}
{% for m in nodes if m | string != n | string %}
  {% set v = states('sensor.danfoss_heltec_valve_temperature_node_' ~ m) | float(none) %}
  {% set r = states('sensor.danfoss_heltec_room_temperature_estimate_node_' ~ m) | float(none) %}
  {% if v is not none and r is not none and v - r >= warm %}{% set ns.warm = true %}{% endif %}
{% endfor %}
{{ ns.warm }}"""

AUTOMATIONS = {
    # Alerts: phone plus a persistent notification in HA.
    'heating_bridge_offline': {
        'alias': 'Heating: Danfoss bridge offline',
        'description': 'The Heltec bridge has been unreachable over MQTT for 10 minutes; heads keep their last setpoint but cannot be changed.',
        'mode': 'single',
        'triggers': [{'trigger': 'state', 'entity_id': BRIDGE, 'to': 'unavailable', 'for': {'minutes': 10}, 'id': 'offline'},
                     {'trigger': 'state', 'entity_id': BRIDGE, 'from': 'unavailable', 'id': 'online'}],
        'conditions': [],
        'actions': [{'choose': [
            {'conditions': [{'condition': 'trigger', 'id': 'offline'}],
             'sequence': notify('Heating bridge offline', 'The Danfoss Heltec bridge has been offline for 10 minutes.')},
            {'conditions': [{'condition': 'trigger', 'id': 'online'},
                            {'condition': 'template', 'value_template': '{{ (now() - trigger.from_state.last_changed).total_seconds() > 600 }}'}],
             'sequence': notify('Heating bridge back online', 'The Danfoss Heltec bridge is reachable again.')}]}]},
    'heating_head_not_reporting': {
        'alias': 'Heating: radiator head not reporting',
        'description': 'A Link head has not been heard for over 45 minutes while the bridge itself is online (battery, range or alarm).',
        'mode': 'parallel',
        'triggers': [{'trigger': 'state', 'entity_id': [f'sensor.danfoss_heltec_last_report_node_{n}' for n in LINK_NODES],
                      'to': 'unavailable', 'not_from': ['unknown', 'unavailable'], 'for': {'minutes': 30}}],
        'conditions': [{'condition': 'not', 'conditions': [{'condition': 'state', 'entity_id': BRIDGE, 'state': 'unavailable'}]}],
        'actions': notify('Radiator head not reporting',
                          "{{ device_attr(device_id(trigger.entity_id), 'name') }} last reported "
                          "{{ trigger.from_state.state | as_datetime | as_local }}. Check batteries or range.")},
    'heating_head_low_battery': {
        'alias': 'Heating: radiator head low battery',
        'description': 'A Link head reports low battery for an hour.',
        'mode': 'parallel',
        'triggers': [{'trigger': 'state', 'entity_id': [f'binary_sensor.danfoss_heltec_low_battery_node_{n}' for n in LINK_NODES],
                      'to': 'on', 'for': {'hours': 1}}],  # also from unknown/unavailable
        'conditions': [],
        'actions': notify('Radiator head low battery', "{{ device_attr(device_id(trigger.entity_id), 'name') }} needs new batteries.")},
    'heating_radiator_not_warming': {
        'alias': 'Heating: radiator not warming',
        'description': 'A Link head has held its valve open for an hour but its valve-side sensor stays near room '
                       'temperature (a working radiator reads several degrees above). Likely a stuck valve pin, '
                       'air in the radiator or a closed lockshield valve.',
        'mode': 'parallel',
        'triggers': [{'trigger': 'numeric_state', 'entity_id': f'sensor.danfoss_heltec_valve_opening_node_{n}',
                      'above': WARM_OPEN, 'for': {'minutes': 60}} for n in LINK_NODES],
        'variables': {'nodes': sorted(LINK_NODES), 'warm': WARM_RISE, **RADIATOR_VARS},
        'conditions': [{'condition': 'template', 'value_template': RADIATOR_COLD}],
        'actions': [{'choose': [
            # If no radiator anywhere is warm, the heating supply itself is off.
            {'conditions': [{'condition': 'template', 'value_template': OTHERS_WARM}],
             'sequence': notify('Check the radiator',
                                "{{ device_attr(device_id(trigger.entity_id), 'name') }}: valve {{ trigger.to_state.state | float | round(0) | int }} % "
                                "open for an hour, but its valve side is only {{ (vt - room) | round(1) }} °C above the room "
                                "(other radiators are warm). Check the head pin: take the head off and press the pin, "
                                "it should spring back. Also check the radiator for air and that its return valve is open.")}],
            'default': notify('No radiator is warming', 'Valves are open but no radiator is getting warm. Is the boiler or heating pump on?')}]},
    'heating_bridge_error': {
        'alias': 'Heating: Danfoss bridge error',
        'description': 'The bridge logged an error event (storage, control or command).',
        'mode': 'queued',
        # Each new error carries a new timestamp, even a repeat of the same text. An
        # error older than 15 minutes is history being replayed (HA or bridge restart).
        'triggers': [{'trigger': 'state', 'entity_id': 'sensor.danfoss_heltec_last_error', 'attribute': 'at'}],
        'conditions': [{'condition': 'template', 'value_template':
                        "{% set at = trigger.to_state.attributes.at | as_datetime(None) %}"
                        "{{ trigger.to_state.state not in ['none', 'unknown', 'unavailable'] and at is not none "
                        "and (now() - at).total_seconds() < 900 }}"}],
        'actions': notify('Heating bridge error', '{{ trigger.to_state.state }}')},
}


# ---------- dashboard ----------

MODE_HELP = {
    'Schedule': 'Each room follows its schedule group: comfort inside the times, eco outside.',
    'At Home': 'Every room at its comfort temperature all day.',
    'Away': 'Every room at the away temperature. Back to Schedule at the return time, if one is set.',
}

STATUS = """{%- set mode = states('input_select.heating_mode') %}
{%- set help = """ + json.dumps(MODE_HELP) + """ %}
**{{ mode }}** · {{ help.get(mode, '') }}
{%- if mode == 'Away' and as_timestamp(states('input_datetime.heating_back_home'), 0) > as_timestamp(now()) %}
Back to Schedule {{ as_timestamp(states('input_datetime.heating_back_home')) | timestamp_custom('%a %d %b, %H:%M') }}.
{%- endif %}
{%- set heads = """ + json.dumps({str(n): ROOMS[k][0] for n, k in LINK_NODES.items()}) + """ %}
{%- set ns = namespace(silent=[], low=[], budget=[]) %}
{%- for node, room in heads.items() %}
{%- if states('sensor.danfoss_heltec_last_report_node_' ~ node) in ['unavailable', 'unknown'] %}{%- set ns.silent = ns.silent + [room ~ ' (' ~ node ~ ')'] %}{%- endif %}
{%- if is_state('binary_sensor.danfoss_heltec_low_battery_node_' ~ node, 'on') %}{%- set ns.low = ns.low + [room] %}{%- endif %}
{%- if states('sensor.danfoss_heltec_setpoint_updates_left_node_' ~ node) | int(999) < 30 %}{%- set ns.budget = ns.budget + [room] %}{%- endif %}
{%- endfor %}
{%- if is_state('sensor.danfoss_heltec_checkpoint_generation', 'unavailable') %}
<ha-alert alert-type="error">The Danfoss bridge is offline. Heads keep their setpoints but cannot be changed.</ha-alert>
{%- endif %}
{%- if ns.silent %}<ha-alert alert-type="warning">Not reporting: {{ ns.silent | join(', ') }}</ha-alert>{%- endif %}
{%- if ns.low %}<ha-alert alert-type="warning">Low battery: {{ ns.low | join(', ') }}</ha-alert>{%- endif %}
{%- if is_state('sensor.trv_office_batterie', 'unavailable') %}<ha-alert alert-type="warning">Office TRV not reporting its battery</ha-alert>
{%- elif states('sensor.trv_office_batterie') | int(100) < 20 %}<ha-alert alert-type="warning">Office TRV battery low</ha-alert>{%- endif %}
{%- if ns.budget %}<ha-alert alert-type="warning">Few setpoint updates left: {{ ns.budget | join(', ') }}</ha-alert>{%- endif %}
{%- set waiting = """ + json.dumps({f'climate.danfoss_heltec_radiator_node_{n}': ROOMS[k][0] + (f' ({n})' if len(ROOMS[k][1]) > 1 else '')
                                    for n, k in LINK_NODES.items()}) + """ %}
{#- A sleeping head picks up a change at its next check-in, about 5 minutes after the last. #}
{%- for c, label in waiting.items() if state_attr(c, 'pending') == true %}
{%- set seen = state_attr(c, 'last_seen') | as_datetime(None) %}
{%- set due = seen + timedelta(minutes=5) if seen else none %}
<ha-alert alert-type="info">{{ label }}: {{ state_attr(c, 'requested_target') }} °C waiting for the head,
{%- if due and due > now() %} expected by about {{ due | as_timestamp | timestamp_custom('%H:%M') }}.
{%- else %} expected at its next check-in.{% endif %} Pressing the head delivers it at once.</ha-alert>
{%- endfor %}"""

PLAN = """{% set mode = states('input_select.heating_mode') %}
{% set rooms = """ + json.dumps({k: [v[0], v[2], v[1]] for k, v in ROOMS.items()}) + """ %}
| Room | Now | Target | Plan |
|:--|--:|--:|:--|
{% for key, r in rooms.items() -%}
{% set t = state_attr(r[1], 'current_temperature') if r[1].startswith('climate.') else states(r[1]) -%}
{% set group = states('input_select.heating_' ~ key ~ '_schedule') -%}
{% set sname = ('Room schedule ' ~ r[0]) if group == 'Own schedule' else ('Heating ' ~ group) -%}
{% set sched = states.schedule | selectattr('name', 'eq', sname) | map(attribute='entity_id') | first | default('') -%}
{% if mode == 'Schedule' and sched -%}
{% set next = state_attr(sched, 'next_event') -%}
{% set plan = ('own schedule' if group == 'Own schedule' else group) ~ ' · ' ~ ('comfort' if is_state(sched, 'on') else 'eco') ~ ((' until ' ~ as_timestamp(next) | timestamp_custom('%a %H:%M')) if next else '') -%}
{% elif mode == 'Schedule' %}{% set plan = 'always comfort' -%}
{% else %}{% set plan = mode | lower -%}{% endif -%}
| {{ r[0] }} | {{ (t | float | round(1) ~ '°') if t | is_number else '—' }} | {{ r[2] | map('state_attr', 'temperature') | reject('none') | map('string') | map('regex_replace', '$', '°') | join(' / ') or '—' }} | {{ plan }} |
{% endfor %}"""

DIAG = """{% set heads = """ + json.dumps(dict([(str(n), ROOMS[k][0]) for n, k in sorted(LINK_NODES.items())] +
                                         [(str(n), name) for n, name in TEST_NODES.items()])) + """ %}
| Head | Last report | Signal | Battery | Valve · radiator side | Updates left |
|:--|:--|--:|:--|--:|--:|
{% for node, room in heads.items() -%}
{% set seen = states('sensor.danfoss_heltec_last_report_node_' ~ node) -%}
{% set rssi = states('sensor.danfoss_heltec_signal_strength_node_' ~ node) -%}
{% set bat = states('binary_sensor.danfoss_heltec_low_battery_node_' ~ node) -%}
| {{ room }} ({{ node }}) | {{ (seen | as_datetime | as_local).strftime('%a %H:%M') if seen | as_datetime else '—' }} | {{ (rssi ~ ' dBm') if rssi | is_number else '—' }} {% set level = states('sensor.danfoss_heltec_battery_level_node_' ~ node) -%}
{% set valve = states('sensor.danfoss_heltec_valve_opening_node_' ~ node) -%}
{% set side = states('sensor.danfoss_heltec_valve_temperature_node_' ~ node) | float(none) -%}
{% set rt = states('sensor.danfoss_heltec_room_temperature_estimate_node_' ~ node) | float(none) -%}
{% set diff = (side - rt) if side is not none and rt is not none else none -%}
| {{ 'LOW' if bat == 'on' else (level ~ ' %') if level | is_number else {'off': 'ok'}.get(bat, '—') }} | {{ (valve | float | round(0) | int ~ ' %') if valve | is_number else '—' }}{{ (' · ' ~ ('%+.1f' | format(diff)) ~ ' °C' ~ (' ⚠️' if valve | float(0) >= """ + str(WARM_OPEN) + """ and diff < """ + str(WARM_RISE) + """ else '')) if diff is not none else '' }} | {{ states('sensor.danfoss_heltec_setpoint_updates_left_node_' ~ node) | replace('unavailable', '—') | replace('unknown', '—') }} |
{% endfor -%}
| Office (Zigbee) | {{ relative_time(states.climate.trv_office.last_reported) }} ago | — | {{ states('sensor.trv_office_batterie') | int(0) }} % | {{ states('sensor.trv_office_pi_heating_demand') }} % | no limit |"""


def head_label(name, heads, head):
    return name if len(heads) == 1 else f"{name} ({head.rsplit('_', 1)[1]})"


def room_tiles(key):
    """One tile per head: heads in the same room regulate on their own sensors."""
    name, heads, *_ = ROOMS[key]
    return [{'type': 'tile', 'entity': head, 'vertical': False,
             'name': head_label(name, heads, head),
             'state_content': ['current_temperature', 'temperature'],
             'features': [{'type': 'target-temperature'}], 'features_position': 'bottom',
             'grid_options': {'columns': 6}} for head in heads]


def dashboard():
    rooms_view = {'title': 'Rooms', 'path': 'rooms', 'icon': 'mdi:radiator', 'type': 'sections', 'max_columns': 3,
                  'sections': [
                      {'type': 'grid', 'cards': [
                          {'type': 'heading', 'heading': 'Heating', 'icon': 'mdi:home-thermometer'},
                          {'type': 'tile', 'entity': 'input_select.heating_mode', 'name': 'Mode',
                           'features': [{'type': 'select-options'}], 'grid_options': {'columns': 12}},
                          {'type': 'conditional', 'conditions': [
                              {'condition': 'state', 'entity': 'input_select.heating_mode', 'state': 'Away'}],
                           'card': {'type': 'entities', 'entities': [
                               {'entity': 'input_datetime.heating_back_home', 'name': 'Back home'}]}},
                          {'type': 'markdown', 'content': STATUS},
                      ]},
                      {'type': 'grid', 'column_span': 2, 'cards':
                          [{'type': 'heading', 'heading': 'Rooms', 'icon': 'mdi:floor-plan'}] +
                          [tile for k in ROOMS for tile in room_tiles(k)]},
                      {'type': 'grid', 'column_span': 3, 'cards': [
                          {'type': 'heading', 'heading': 'Today', 'icon': 'mdi:calendar-today'},
                          {'type': 'markdown', 'content': PLAN}]},
                  ]}
    settings_view = {'title': 'Schedules & temperatures', 'path': 'settings', 'icon': 'mdi:calendar-clock',
                     'type': 'sections', 'max_columns': 3, 'sections': [
        {'type': 'grid', 'column_span': 2, 'cards': [
            {'type': 'custom:heating-schedule-card', 'title': 'Schedule groups',
             'rooms': [{'name': name, 'select': f'input_select.heating_{key}_schedule'} for key, (name, *_) in ROOMS.items()]}]},
        {'type': 'grid', 'cards': [
            {'type': 'heading', 'heading': 'Room temperatures', 'icon': 'mdi:thermometer'},
            {'type': 'markdown', 'content': '**Comfort** inside a group\'s periods, **eco** outside them.'},
            {'type': 'entities', 'entities': [row for key, (name, *_) in ROOMS.items() for row in (
                {'type': 'section', 'label': name},
                {'entity': f'input_number.heating_{key}_comfort', 'name': 'Comfort'},
                {'entity': f'input_number.heating_{key}_eco', 'name': 'Eco'})]},
            {'type': 'heading', 'heading': 'Away', 'heading_style': 'subtitle'},
            {'type': 'entities', 'entities': [
                {'entity': 'input_number.heating_holiday_temp', 'name': 'Away temperature'},
                {'entity': 'input_datetime.heating_back_home', 'name': 'Back home (ends Away)'}]}]}]}
    # Same axes on every graph so rooms compare at a glance; fit_y_data widens a
    # range only if a value falls outside it.
    temp_axis = {'min_y_axis': 14, 'max_y_axis': 26, 'fit_y_data': True}
    valve_axis = {'min_y_axis': 0, 'max_y_axis': 100}
    curves_view = {'title': 'Curves', 'path': 'curves', 'icon': 'mdi:chart-bell-curve-cumulative',
                   'type': 'sections', 'max_columns': 3, 'sections': [
        {'type': 'grid', 'cards': [
            {'type': 'heading', 'heading': name, 'heading_style': 'subtitle'},
            {'type': 'history-graph', 'hours_to_show': 48, **temp_axis,
             'entities': [{'entity': h, 'name': head_label(name, heads, h)} for h in heads]},
            {'type': 'history-graph', 'hours_to_show': 48, **valve_axis,
             'entities': [{'entity': f'sensor.danfoss_heltec_valve_opening_node_{n}', 'name': f'Valve {n}'}
                          for n, k in LINK_NODES.items() if ROOMS[k][0] == name] +
                         ([{'entity': 'sensor.trv_office_pi_heating_demand', 'name': 'Valve'}] if heads == ['climate.trv_office'] else [])}]}
        for name, heads, temp, *_ in ROOMS.values()]}
    diag_view = {'title': 'Diagnostics', 'path': 'diagnostics', 'icon': 'mdi:stethoscope',
                 'type': 'sections', 'max_columns': 3, 'sections': [
        {'type': 'grid', 'column_span': 2, 'cards': [
            {'type': 'heading', 'heading': 'Radiator heads', 'icon': 'mdi:radiator'},
            {'type': 'markdown', 'content': DIAG}]},
        {'type': 'grid', 'cards': [
            {'type': 'heading', 'heading': 'Danfoss bridge', 'icon': 'mdi:access-point'},
            {'type': 'entities', 'entities': [
                {'entity': 'sensor.danfoss_heltec_started', 'name': 'Running since', 'format': 'relative'},
                {'entity': 'sensor.danfoss_heltec_reset_reason', 'name': 'Last restart cause'},
                {'entity': 'sensor.danfoss_heltec_wifi_signal', 'name': 'WiFi signal'},
                {'entity': 'sensor.danfoss_heltec_last_error', 'name': 'Last error'},
                {'entity': 'sensor.danfoss_heltec_last_event', 'name': 'Last event'},
                {'entity': 'sensor.danfoss_heltec_checkpoint_failures', 'name': 'Checkpoint failures'},
                {'entity': 'sensor.danfoss_heltec_dropped_events', 'name': 'Dropped events'},
                {'entity': 'sensor.danfoss_heltec_lowest_free_memory', 'name': 'Lowest free memory'},
                {'entity': 'binary_sensor.danfoss_heltec_autostart', 'name': 'Resumes control after restart'},
                {'entity': 'button.danfoss_heltec_restart', 'name': 'Restart bridge'}]},
            {'type': 'heading', 'heading': 'Automations', 'heading_style': 'subtitle'},
            {'type': 'entities', 'entities': [
                'automation.heating_controller',
                'automation.heating_danfoss_bridge_offline', 'automation.heating_radiator_head_not_reporting',
                'automation.heating_radiator_head_low_battery', 'automation.heating_danfoss_bridge_error']}]},
        {'type': 'grid', 'column_span': 3, 'cards': [
            {'type': 'history-graph', 'title': 'Head signal (7 days)', 'hours_to_show': 168,
             'entities': [f'sensor.danfoss_heltec_signal_strength_node_{n}' for n in sorted(LINK_NODES)]}]}]}
    return {'title': 'Home Heating', 'views': [rooms_view, settings_view, curves_view, diag_view]}


async def main():
    states = {s['entity_id']: s for s in rest('GET', '/api/states')}
    async with Ws() as ws:
        created = await ensure_helpers(ws, states)
        for domain, entity_id in REMOVED_HELPERS:
            if entity_id in states:
                say('delete', entity_id)
                if not DRY:
                    entry = await ws(type='config/entity_registry/get', entity_id=entity_id)
                    await ws(type=f'{domain}/delete', **{f'{domain}_id': entry['unique_id']})
        await ensure_living_room_mean(ws, states)
        await remove_old_group_states(ws, states)
        await ensure_card_resource(ws)
        await normalise_head_entity_ids(ws)
        schedules = await schedule_entities(ws)
        current_dashboard = await ws(type='lovelace/config', url_path='home-heating')
    if not DRY:
        await set_initial(created)
    # Write only what changed: saving reloads the automation, which would cancel a
    # schedule trigger's pending one-minute hold.
    def ensure_config(kind, oid, body):
        try:
            current = rest('GET', f'/api/config/{kind}/config/{oid}', quiet=True)
        except LookupError:
            current = None
        stored = {**body, **({'id': current['id']} if current and 'id' in current else {})}  # automations only
        if current and json.loads(json.dumps(stored)) == current:
            return
        say(kind, oid)
        if not DRY:
            rest('POST', f'/api/config/{kind}/config/{oid}', body)
    ensure_config('script', 'heating_apply', SCRIPT)
    for aid, body in {'heating_controller': controller(schedules), **AUTOMATIONS}.items():
        ensure_config('automation', aid, body)
    for aid in REMOVED_AUTOMATIONS:
        if f'automation.{aid}' in states or any(s.get('attributes', {}).get('id') == aid for s in states.values()):
            say('delete automation', aid)
            if not DRY:
                rest('DELETE', f'/api/config/automation/config/{aid}')
    for entity_id in OLD_AUTOMATIONS:
        if states.get(entity_id, {}).get('state') == 'on':
            say('disable', entity_id)
            if not DRY:
                rest('POST', '/api/services/automation/turn_off', {'entity_id': entity_id})
    if json.loads(json.dumps(dashboard())) != current_dashboard:
        say('dashboard home-heating')
        if not DRY:
            async with Ws() as ws:
                await ws(type='lovelace/config/save', url_path='home-heating', config=dashboard())


if __name__ == '__main__':
    asyncio.run(main())
