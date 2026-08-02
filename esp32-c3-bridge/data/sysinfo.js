fetch("/sysinfo")
.then(r => r.json())
.then(data => {
    let html = "";

    for (const [key, value] of Object.entries(data)) {
        if (typeof value === "object" && value !== null) {
            html += `<tr><th colspan="2">${key}</th></tr>`;

            for (const [k, v] of Object.entries(value)) {
                html += `<tr><td>${k}</td><td>${v}</td></tr>`;
            }
        } else {
            html += `<tr><td>${key}</td><td>${value}</td></tr>`;
        }
    }

    document.getElementById("sysinfo").innerHTML = html;
});