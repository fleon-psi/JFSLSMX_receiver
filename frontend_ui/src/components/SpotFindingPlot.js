import React, {Component} from 'react';
import Plot from 'react-plotly.js';

class SpotFindingPlot extends Component {
    state = {
        sequence: 0,
        data_graph1: {},
        data_graph2: {}
    }

    componentDidMount() {
        this.interval = setInterval(() => this.updateImg(), 1000);
    }

    componentWillUnmount() {
        clearInterval(this.interval);
    }

    updateImg() {
        fetch('/jf/spot/sequence')
            .then(res => res.json())
            .then(data => { if (this.state.sequence !== data.sequence) {
                this.setState({sequence: data.sequence});
                fetch('/jf/spot/per_angle')
                    .then(res => res.json())
                    .then(data => this.setState({ data_graph1: [{y: data.count, type: "scatter"}]}) );
                fetch('/jf/spot/resolution')
                    .then(res => res.json())
                    .then(data => this.setState({ data_graph2: [{x: data.one_over_d2, y: data.log_meanI, type: "scatter"}]}) );
            }
            });

    }

    render() {
        return <div>
            <Plot data={this.state.data_graph1}
                  layout = {{
                      xaxis: {title: {text: "Omega [deg.]"}},
                      yaxis: {title: {text: "Number of spots"}}
                  }} />
            <Plot data={this.state.data_graph2}
                  layout= {{
                      xaxis: {title: {text: '1/d<sup>2</sup> [A<sup>-2</sup>]'}},
                      yaxis: {title: {text: 'log(&lt;I&gt;)'}}
                  }} />
        </div>
    }
}

export default SpotFindingPlot;
