const demoIncidents = [
  {
    time: "00:12:44",
    direction: "EAST -> WEST",
    path: "EAST -> SOUTH -> WEST",
    classification: "HUMAN",
    threat: "HIGH",
    confidence: 78,
    alertLevel: 3,
    durationSec: 4.2,
    nodeCount: 3,
    pirCount: 11,
    vibCount: 7,
    minDistance: 18
  },
  {
    time: "00:18:02",
    direction: "NORTH -> SOUTH",
    path: "NORTH -> SOUTH",
    classification: "ANIMAL",
    threat: "MEDIUM",
    confidence: 68,
    alertLevel: 2,
    durationSec: 2.6,
    nodeCount: 2,
    pirCount: 5,
    vibCount: 2,
    minDistance: 31
  },
  {
    time: "00:25:19",
    direction: "WEST -> EAST",
    path: "WEST -> SOUTH -> EAST",
    classification: "VEHICLE",
    threat: "CRITICAL",
    confidence: 82,
    alertLevel: 3,
    durationSec: 3.1,
    nodeCount: 3,
    pirCount: 6,
    vibCount: 9,
    minDistance: 14
  }
];

let incidents = [...demoIncidents];

const rows = document.querySelector("#incidentRows");
const jsonInput = document.querySelector("#jsonInput");

function render() {
  rows.innerHTML = incidents.map((incident) => `
    <tr>
      <td>${incident.time}</td>
      <td>${incident.direction}</td>
      <td><strong>${incident.classification}</strong></td>
      <td>${incident.threat}</td>
      <td>${incident.confidence}%</td>
      <td>Level ${incident.alertLevel}</td>
      <td>${incident.path}</td>
    </tr>
  `).join("");

  const latest = incidents[0];

  document.querySelector("#currentThreat").textContent = latest.threat;
  document.querySelector("#currentClass").textContent =
    `${latest.classification}, ${latest.confidence}% confidence`;
  document.querySelector("#currentDirection").textContent = latest.direction;
  document.querySelector("#alertLevel").textContent = latest.alertLevel;
  document.querySelector("#incidentPath").textContent = latest.path;
  document.querySelector("#incidentDuration").textContent =
    `${latest.durationSec} sec`;
  document.querySelector("#incidentNodes").textContent =
    `${latest.nodeCount} active`;
  document.querySelector("#incidentDistance").textContent =
    `${latest.minDistance} cm`;
  document.querySelector("#incidentEvidence").textContent =
    `PIR ${latest.pirCount} / VIB ${latest.vibCount}`;
}

function addIncidentFromInput() {
  try {
    const parsed = JSON.parse(jsonInput.value.trim());
    incidents = [parsed, ...incidents].slice(0, 12);
    render();
  } catch (error) {
    alert("The pasted incident is not valid JSON yet. Copy only the object after the gateway JSON label.");
  }
}

document.querySelector("#addIncident").addEventListener("click", addIncidentFromInput);

document.querySelector("#loadDemo").addEventListener("click", () => {
  incidents = [...demoIncidents];
  render();
});

render();
