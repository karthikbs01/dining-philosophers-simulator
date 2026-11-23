import React from 'react';
import { Armchair, Utensils, GitPullRequest, CircleDot } from 'lucide-react';

export function StateLabel({ state }) {
  return (
    <span className={'state-label ' + state.toLowerCase()}>
      <i />
      {state}
    </span>
  );
}

function point(index, count, radius) {
  const angle = (2 * Math.PI * index) / count - Math.PI / 2;
  return {
    x: 210 + radius * Math.cos(angle),
    y: 174 + radius * Math.sin(angle),
  };
}

export function DiningTable({ state }) {
  const { people, owners, queue, eaters } = state;
  return (
    <section className="panel table-panel">
      <header className="panel-head">
        <h2>
          <CircleDot />
          Dining table
        </h2>
        <span className="counter">{eaters} eating</span>
      </header>
      <div className="schematic">
        <svg
          viewBox="0 0 420 348"
          role="img"
          aria-label={
            'Circular table with ' +
            people.length +
            ' philosophers and their forks'
          }
        >
          <defs>
            <radialGradient id="surface" cx="36%" cy="30%" r="85%">
              <stop stopColor="#f5f3ff" />
              <stop offset="1" stopColor="#ded9f4" />
            </radialGradient>
            <pattern
              id="hatch"
              width="5"
              height="5"
              patternUnits="userSpaceOnUse"
              patternTransform="rotate(45)"
            >
              <line y2="5" stroke="#afa5d8" strokeWidth=".65" />
            </pattern>
          </defs>
          <circle
            cx="210"
            cy="174"
            r="134"
            fill="none"
            stroke="#c9c4dc"
            strokeDasharray="3 5"
          />
          <circle cx="210" cy="177" r="104" fill="#c6c0df" />
          <circle
            cx="210"
            cy="174"
            r="104"
            fill="url(#surface)"
            stroke="#b8afd7"
          />
          <circle
            cx="210"
            cy="174"
            r="99"
            fill="none"
            stroke="#fefcff"
            strokeWidth="2"
          />
          <circle cx="210" cy="174" r="94" fill="url(#hatch)" opacity=".5" />
          <circle cx="210" cy="174" r="62" fill="#f1eefb" stroke="#d1c9e9" />
          <Utensils
            x="199"
            y="144"
            width="22"
            height="22"
            stroke="#7666a9"
            strokeWidth="1.7"
          />
          <text className="table-title" x="210" y="185">
            Shared table
          </text>
          <text className="table-meta" x="210" y="201">
            {people.length} forks
          </text>
          {owners.map((owner, i) => {
            const p = point(i - 0.5, people.length, 84);
            return (
              <g
                key={i}
                transform={'translate(' + p.x + ' ' + p.y + ')'}
                className={owner < 0 ? 'fork' : 'fork held'}
              >
                <title>
                  {'F' + i + (owner < 0 ? ' is free' : ' held by P' + owner)}
                </title>
                <rect
                  x="-21"
                  y="-10"
                  width="42"
                  height={owner < 0 ? 20 : 31}
                  rx="8"
                />
                <path
                  d="M-13 -5v4q3 4 6 0v-4m-3 0V6"
                  fill="none"
                  stroke="currentColor"
                  strokeWidth="1.4"
                  strokeLinecap="round"
                />
                <text x="5" y="4">
                  {'F' + i}
                </text>
                {owner >= 0 && (
                  <text x="0" y="16">
                    {'P' + owner}
                  </text>
                )}
              </g>
            );
          })}
          {people.map((p, i) => {
            const pos = point(i, people.length, 137);
            return (
              <g
                key={i}
                transform={'translate(' + pos.x + ' ' + pos.y + ')'}
                className={'seat ' + p.state.toLowerCase()}
              >
                <title>{'P' + i + ' · ' + p.state}</title>
                <rect x="-25" y="-21" width="50" height="42" rx="12" />
                <Armchair
                  x="-8"
                  y="-16"
                  width="16"
                  height="16"
                  strokeWidth="1.8"
                />
                <text y="14">{'P' + i}</text>
              </g>
            );
          })}
        </svg>
      </div>
      <div className="legend">
        {['Thinking', 'Hungry', 'Eating', 'Finished'].map((s) => (
          <StateLabel key={s} state={s} />
        ))}
      </div>
      <div className="queue">
        <span>
          <GitPullRequest size={15} /> FIFO queue
        </span>
        <strong>
          {queue.length ? queue.map((id) => 'P' + id).join(' › ') : 'Empty'}
        </strong>
      </div>
    </section>
  );
}
