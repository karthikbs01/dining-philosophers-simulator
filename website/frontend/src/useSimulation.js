import { useEffect, useState } from 'react';

export function useSimulation() {
  const [state, setState] = useState(null);
  const [connection, setConnection] = useState('Connecting');
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);
  useEffect(() => {
    let stream, retry;
    let firstFrame;
    function receive(event) {
      const packet = JSON.parse(event.data);
      const replace = firstFrame;
      firstFrame = false;
      setState((previous) => {
        if (replace || previous?.generation !== packet.generation)
          return packet;
        const newer = packet.events.filter((item) => item.seq > previous.seq);
        return {
          ...packet,
          events: newer.length
            ? previous.events.concat(newer)
            : previous.events,
        };
      });
      setConnection('Connected');
    }
    function connect() {
      stream = new EventSource('/api/events');
      stream.onopen = () => {
        firstFrame = true;
      };
      stream.onmessage = receive;
      stream.onerror = () => {
        setConnection('Reconnecting');
        // EventSource retries dropped sockets itself. An HTTP error can close it permanently.
        if (stream.readyState === EventSource.CLOSED)
          retry = setTimeout(connect, 1500);
      };
    }
    connect();
    return () => {
      clearTimeout(retry);
      stream.close();
    };
  }, []);
  async function command(action, config) {
    if (!state || busy || connection !== 'Connected') return;
    setError('');
    setBusy(true);
    try {
      const response = await fetch('/api/' + action, {
        method: 'POST',
        headers: { 'X-Dining-Control': '1' },
        body: new URLSearchParams({ generation: state.generation, ...config }),
      });
      if (!response.ok)
        throw new Error((await response.text()) || 'Command failed.');
    } catch (err) {
      setError(err.message);
    } finally {
      setBusy(false);
    }
  }
  return { state, connection, error, busy, command };
}
