import React, {Component} from 'react';
import Plot from 'react-plotly.js';

class SpotFindingPlot extends Component {
  state = {
     sequence: 0,
     spots: []
  }

  componentDidMount() {
    this.interval = setInterval(() => this.updateImg(), 1000);
  }

  componentWillUnmount() {
    clearInterval(this.interval);
  }

  updateImg() {
    fetch('http://' + window.location.hostname + ':5232/spot_sequence', {crossDomain:true})
    .then(res => res.json())
    .then(data => { if (this.state.sequence !== data.sequence) {
        this.setState({sequence: data.sequence});
        fetch('http://' + window.location.hostname + ':5232/spot_count', {crossDomain:true})
        .then(res => res.json())
        .then( data1 => this.setState({ spots: data1}) );
        }
    });
    
  }

  render() {
     return <Plot data={[
               {y: this.state.spots, type: 'scatter'},
               {xaxis: {title: "Omega [deg.]"}}]}
            />
  }
}

export default SpotFindingPlot;
