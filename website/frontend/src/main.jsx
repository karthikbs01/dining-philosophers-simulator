import React, { useEffect, useState } from 'react';
import { createRoot } from 'react-dom/client';
import {
  Activity,
  Layers3,
  List,
  Pause,
  Play,
  RotateCcw,
  Settings2,
  Terminal,
  Timer,
  Utensils,
  Users,
  Minus,
  Plus,
  ChartNoAxesColumnIncreasing,
  Radio,
} from 'lucide-react';
import { useSimulation } from './useSimulation';
import { DiningTable, StateLabel } from './Table';
import './style.css';

const fields = [
  ['count', 'Philosophers', 2, 10, 1],
  ['rounds', 'Eating rounds', 1, 100, 1],
  ['thinkMs', 'Thinking time', 0, 60000, 50],
  ['eatMs', 'Eating time', 0, 60000, 50],
];
const seconds = (ms) => (ms / 1000).toFixed(2);

function Controls({ state, busy, command }) {
  const [draft, setDraft] = useState(state.config);
  useEffect(() => {
    setDraft(state.config);
  }, [state.generation]);
  const active = ['Running', 'Paused'].includes(state.status);
  const valid = fields.every(
    ([key, , min, max]) =>
      /^\d+$/.test(draft[key]) && +draft[key] >= min && +draft[key] <= max,
  );
  const change = (key, val) => setDraft((old) => ({ ...old, [key]: val }));
  return (
    <section className="panel controls">
      <header className="panel-head">
        <h2>
          <Settings2 />
          Configuration
        </h2>
      </header>
      <form
        onSubmit={(e) => {
          e.preventDefault();
          if (valid) command('start', draft);
        }}
      >
        <fieldset disabled={active || busy}>
          {fields.map(([key, label, min, max, step]) => (
            <div className="field" key={key}>
              <label htmlFor={key}>{label}</label>
              <div className="number-box">
                <button
                  type="button"
                  aria-label={'Decrease ' + label}
                  disabled={+draft[key] <= min}
                  onClick={() => change(key, Math.max(min, +draft[key] - step))}
                >
                  <Minus />
                </button>
                <div>
                  <input
                    id={key}
                    type="number"
                    required
                    min={min}
                    max={max}
                    value={draft[key]}
                    onChange={(e) => change(key, e.target.value)}
                  />
                  {key.endsWith('Ms') && <span>ms</span>}
                </div>
                <button
                  type="button"
                  aria-label={'Increase ' + label}
                  disabled={+draft[key] >= max}
                  onClick={() => change(key, Math.min(max, +draft[key] + step))}
                >
                  <Plus />
                </button>
              </div>
            </div>
          ))}
        </fieldset>
        {!valid && (
          <p className="error" role="alert">
            Use 2–10 philosophers, 1–100 rounds and 0–60000 ms.
          </p>
        )}
        <div className="actions">
          <button className="primary" disabled={busy || active || !valid}>
            <Play fill="currentColor" />
            Start simulation
          </button>
          <div className="paired">
            <button
              type="button"
              disabled={busy || !active}
              onClick={() =>
                command(state.status === 'Paused' ? 'resume' : 'pause')
              }
            >
              {state.status === 'Paused' ? <Play /> : <Pause />}
              {state.status === 'Paused' ? 'Resume' : 'Pause'}
            </button>
            <button
              type="button"
              disabled={busy || state.status === 'Idle'}
              onClick={() => command('reset')}
            >
              <RotateCcw />
              Reset
            </button>
          </div>
        </div>
      </form>
    </section>
  );
}

function Metrics({ state }) {
  const meals = state.people.reduce((sum, p) => sum + p.meals, 0);
  const cards = [
    [
      Utensils,
      'Meals completed',
      meals,
      '/ ' + state.config.count * state.config.rounds,
      'violet',
    ],
    [Timer, 'Active runtime', seconds(state.elapsed), 's', 'blue'],
    [
      ChartNoAxesColumnIncreasing,
      'Fork utilization',
      state.utilization.toFixed(1),
      '%',
      'teal',
    ],
    [Users, 'Peak eaters', state.peak, '', 'orange'],
  ];
  return (
    <div className="metrics">
      {cards.map(([Icon, label, value, unit, color]) => (
        <div className="metric" key={label}>
          <span className={'metric-icon ' + color}>
            <Icon />
          </span>
          <div>
            <span className="metric-label">{label}</span>
            <div className="value">
              {value} <small>{unit}</small>
            </div>
          </div>
        </div>
      ))}
    </div>
  );
}

function Results({ state }) {
  return (
    <section className="panel">
      <header className="panel-head">
        <h2>
          <List />
          Philosopher results
        </h2>
      </header>
      <div className="table-scroll">
        <table>
          <thead>
            <tr>
              {['Seat', 'State', 'Meals', 'Avg wait', 'Max wait'].map((h) => (
                <th key={h}>{h}</th>
              ))}
            </tr>
          </thead>
          <tbody>
            {state.people.map((p, i) => (
              <tr key={i}>
                <td>
                  <b>P{i}</b>
                </td>
                <td>
                  <StateLabel state={p.state} />
                </td>
                <td>{p.meals}</td>
                <td>{p.avgWait.toFixed(1)} ms</td>
                <td>{p.maxWait.toFixed(1)} ms</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </section>
  );
}

function Journal({ events }) {
  const [limit, setLimit] = useState(40);
  return (
    <section className="panel journal">
      <header className="panel-head">
        <h2>
          <Terminal />
          Event log
        </h2>
        <span className="counter">{events.length} events</span>
      </header>
      {events.length ? (
        <div
          className="log-scroll"
          tabIndex={0}
          aria-label="Events, newest first"
        >
          {events
            .slice(-limit)
            .reverse()
            .map((e) => (
              <div className="log-row" key={e.seq}>
                <time>{seconds(e.ms)} s</time>
                <b>{e.id < 0 ? 'SYS' : 'P' + e.id}</b>
                <span>{e.message}</span>
              </div>
            ))}
          {events.length > limit && (
            <button className="older" onClick={() => setLimit(limit + 100)}>
              Show older events
            </button>
          )}
        </div>
      ) : (
        <div className="empty-log">
          <Terminal />
          <p>Events will appear when the simulation starts.</p>
        </div>
      )}
    </section>
  );
}

function App() {
  const { state, connection, error, busy, command } = useSimulation();
  return (
    <>
      <header className="topbar">
        <div className="bar-inner">
          <div className="brand">
            <span className="brand-mark">
              <Layers3 />
            </span>
            Sync Lab
          </div>
          <span className="course">Operating systems</span>
          <span
            className={
              'connection ' + (connection === 'Connected' ? 'online' : '')
            }
          >
            <i />
            {connection}
          </span>
        </div>
      </header>
      <main>
        <div className="intro">
          <div>
            <h1>Dining philosophers</h1>
            <p>
              Explore thread synchronization and fork sharing with a strict FIFO
              scheduler.
            </p>
          </div>
          {state && (
            <span className={'session ' + state.status.toLowerCase()}>
              <Activity size={15} />
              {state.status === 'Idle' ? 'Session idle' : state.status}
            </span>
          )}
        </div>
        {connection === 'Reconnecting' && (
          <p className="notice" role="alert">
            Connection interrupted. Reconnecting to the C++ server…
          </p>
        )}
        {error && (
          <p className="notice" role="alert">
            {error}
          </p>
        )}
        {!state ? (
          <div className="loading" role="status">
            <Radio /> Waiting for the simulation server…
          </div>
        ) : (
          <>
            <Metrics state={state} />
            <div className="workbench">
              <DiningTable state={state} />
              <Controls
                state={state}
                busy={busy || connection !== 'Connected'}
                command={command}
              />
            </div>
            <div className="details">
              <Results state={state} />
              <Journal key={state.generation} events={state.events} />
            </div>
          </>
        )}
      </main>
    </>
  );
}
createRoot(document.getElementById('root')).render(<App />);
