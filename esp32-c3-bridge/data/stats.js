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
}

loadStats().catch(error => {
    console.error('Failed to load stats:', error);
});
