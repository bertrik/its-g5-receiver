function formatDate(unixSeconds) {
  if (!unixSeconds) {
    return "-";
  }
  return new Date(unixSeconds * 1000).toLocaleString();
}

function formatDuration(seconds) {
  const days = Math.floor(seconds / 86400);
  const hours = Math.floor((seconds % 86400) / 3600);
  const minutes = Math.floor((seconds % 3600) / 60);
  const secs = seconds % 60;
  return `${days}d ${hours}h ${minutes}m ${secs}s`;
}

async function loadStats() {
    const response = await fetch('/stats');

    if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
    }

    const stats = await response.json();

    const counts = stats.counts;
    const max = Math.max(...counts, 1); // avoid division by zero

    const chart = document.getElementById('chart');

    chart.innerHTML = counts
        .map((count, minute) => `
            <div
                class="bar"
                title="-${minute.toString()}min: ${count} messages"
                style="height:${Math.max(2, (count / max) * 100)}%">
            </div>
        `)
        .join('');

    document.getElementById('timestamp').textContent = formatDate(stats.timestamp);
    document.getElementById('latest').textContent = formatDate(stats.latest);
    document.getElementById('uptime').textContent = formatDuration(stats.uptime);
}

loadStats().catch(error => {
    console.error('Failed to load stats:', error);
});
